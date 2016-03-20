///
/// @file  phi_libdivide.cpp
/// @brief This is an optimized version of phi which uses
///        libdivide. libdivide allows to replace expensive integer
///        divides with comparatively cheap multiplication and
///        bitshifts.
///
/// Copyright (C) 2016 Kim Walisch, <kim.walisch@gmail.com>
///
/// This file is distributed under the BSD License. See the COPYING
/// file in the top level directory.
///

#include <PiTable.hpp>
#include <primecount-internal.hpp>
#include <primecount.hpp>
#include <generate.hpp>
#include <pmath.hpp>
#include <PhiTiny.hpp>
#include <min_max.hpp>

#include <libdivide.h>
#include <stdint.h>
#include <algorithm>
#include <vector>
#include <limits>

#ifdef _OPENMP
  #include <omp.h>
#endif

using namespace std;
using namespace primecount;

namespace {

class PhiCache
{
public:
  PhiCache(vector<int32_t>& primes,
           vector<libdivide::divider<uint64_t> >& fastdiv,
           PiTable& pi) :
    primes_(primes),
    fastdiv_(fastdiv),
    pi_(pi),
    bytes_(0)
  {
    size_t max_size = CACHE_A_LIMIT + 1;
    cache_.resize(min(primes.size(), max_size));
  }

  /// Calculate phi(x, a) using the recursive formula:
  /// phi(x, a) = phi(x, a - 1) - phi(x / primes_[a], a - 1)
  ///
  template <int SIGN>
  int64_t phi(int64_t x, int64_t a)
  {
    int64_t sum = 0;

    if (x <= primes_[a])
      sum = SIGN;
    else if (is_phi_tiny(a))
      sum = phi_tiny(x, a) * SIGN;
    else if (is_phi_by_pix(x, a))
      sum = (pi_[x] - a + 1) * SIGN;
    else
    {
      int64_t sqrtx = isqrt(x);
      int64_t pi_sqrtx = a;

      if (sqrtx < pi_.size() && sqrtx < primes_[a])
        pi_sqrtx = pi_[sqrtx];

      // Move out of the loop the calculations where phi(x2, a2) = 1
      // phi(x, a) = 1 if primes_[a] >= x
      // x2 = x / primes_[a2 + 1]
      // phi(x2, a2) = 1 if primes_[a2] >= x / primes_[a2 + 1]
      // phi(x2, a2) = 1 if primes_[a2] >= sqrt(x)
      // phi(x2, a2) = 1 if a2 >= pi(sqrt(x))
      // \sum_{a2 = pi(sqrt(x))}^{a - 1} phi(x2, a2) = a - pi(sqrt(x))
      //
      sum = (a - pi_sqrtx) * -SIGN;

      // phi(x, c) = phi(x, 1) - \sum_{a2 = 1}^{c - 1} phi(x / primes_[a2 + 1], a2)
      int64_t c = min(PhiTiny::max_a(), pi_sqrtx);
      sum += phi_tiny(x, c) * SIGN;

      for (int64_t a2 = c; a2 < pi_sqrtx; a2++)
      {
        int64_t x2 = ((uint64_t) x) / fastdiv_[a2 + 1];
        if (is_cached(x2, a2))
          sum += cache_[a2][x2] * -SIGN;
        else
          sum += phi<-SIGN>(x2, a2);
      }
    }

    if (write_to_cache(x, a))
      cache_[a][x] = (uint16_t) (sum * SIGN);

    return sum;
  }

private:
  enum
  {
    /// Cache phi(x, a) results if a <= CACHE_A_LIMIT
    CACHE_A_LIMIT = 500,
    /// Keep the cache size below CACHE_BYTES_LIMIT per thread
    CACHE_BYTES_LIMIT = 16 << 20
  };

  vector<vector<uint16_t> > cache_;
  vector<int32_t>& primes_;
  vector<libdivide::divider<uint64_t> >& fastdiv_;
  PiTable& pi_;
  int64_t bytes_;

  int64_t cache_size(int64_t a) const
  {
    return (int64_t) cache_[a].size();
  }

  bool is_phi_by_pix(int64_t x, int64_t a) const
  {
    return x < pi_.size() &&
           x < isquare(primes_[a + 1]);
  }

  bool is_cached(int64_t x, int64_t a) const
  {
    return a <= CACHE_A_LIMIT && 
           x < cache_size(a) && 
           cache_[a][x] != 0;
  }

  bool write_to_cache(int64_t x, int64_t a)
  {
    if (a > CACHE_A_LIMIT || 
        x > numeric_limits<uint16_t>::max())
      return false;

    // we need to increase cache size
    if (x >= cache_size(a))
    {
      if (bytes_ > CACHE_BYTES_LIMIT)
        return false;
      bytes_ += (x + 1 - cache_size(a)) * 2;
      cache_[a].resize(x + 1, 0);
    }

    return true;
  }
};

} // namespace

namespace primecount {

/// Partial sieve function (a.k.a. Legendre-sum).
/// phi(x, a) counts the numbers <= x that are not divisible
/// by any of the first a primes.
///
int64_t phi(int64_t x, int64_t a, int threads)
{
  if (x < 1) return 0;
  if (a > x) return 1;
  if (a < 1) return x;

  print("");
  print("=== phi(x, a) ===");
  print("Count the numbers <= x coprime to the first a primes");

  double time = get_wtime();
  int64_t sum = 0;

  if (is_phi_tiny(a))
    sum = phi_tiny(x, a);
  else
  {
    vector<int32_t> primes = generate_n_primes(a);

    if (primes.at(a) >= x)
      sum = 1;
    else
    {
      // use large pi(x) lookup table for speed
      int64_t sqrtx = isqrt(x);
      PiTable pi(max(sqrtx, primes[a]));
      vector<libdivide::divider<uint64_t> > fastdiv(primes.begin(), primes.end());
      PhiCache cache(primes, fastdiv, pi);

      int64_t pi_sqrtx = min(pi[sqrtx], a); 
      sum = x - a + pi_sqrtx;

      int64_t thread_threshold = ipow((int64_t) 10, 14) / primes[a];
      threads = validate_threads(threads, x, thread_threshold);

      // this loop scales only up to about 8 CPU cores
      threads = min(8, threads);

      #pragma omp parallel for firstprivate(cache) num_threads(threads) schedule(dynamic, 16) reduction(+: sum)
      for (int64_t a2 = 0; a2 < pi_sqrtx; a2++)
        sum += cache.phi<-1>(x / primes[a2 + 1], a2);
    }
  }

  print("phi", sum, time);
  return sum;
}

} // namespace