///
/// @file  pi_lmo_parallel1.cpp
/// @brief Parallel implementation of the Lagarias-Miller-Odlyzko
///        prime counting algorithm using OpenMP. This implementation
///        is based on pi_lmo4(x).
///
/// Copyright (C) 2014 Kim Walisch, <kim.walisch@gmail.com>
///
/// This file is distributed under the BSD License. See the COPYING
/// file in the top level directory.
///

#include "tos_counters.hpp"

#include <primecount-internal.hpp>
#include <primesieve.hpp>
#include <pmath.hpp>
#include <PhiTiny.hpp>

#include <stdint.h>
#include <algorithm>
#include <vector>

#ifdef _OPENMP
  #include <omp.h>
  #include <get_omp_threads.hpp>
#endif

using namespace std;

#ifdef _OPENMP

namespace {

template <typename T1, typename T2>
void cross_off(int64_t prime,
               int64_t low,
               int64_t high,
               int64_t& next_multiple,
               T1& sieve,
               T2& counters)
{
  int64_t segment_size = sieve.size();
  int64_t k = next_multiple + prime * (~next_multiple & 1);

  for (; k < high; k += prime * 2)
  {
    if (sieve[k - low])
    {
      sieve[k - low] = 0;
      cnt_update(counters, k - low, segment_size);
    }
  }
  next_multiple = k;
}

/// Compute the S2 contribution for the interval
/// [low_thread, low_thread + segments * segment_size[.
/// The missing special leaf contributions for the interval
/// [1, low_thread[ are later reconstructed and added in
/// the calling (parent) S2 function.
///
int64_t S2_thread(int64_t x,
                  int64_t y,
                  int64_t pi_y,
                  int64_t c,
                  int64_t limit,
                  int64_t segments,
                  int64_t segment_size,
                  int64_t segments_per_thread,
                  int64_t thread_num,
                  vector<int32_t>& primes,
                  vector<int32_t>& lpf,
                  vector<int32_t>& mu,
                  vector<int64_t>& mu_sum,
                  vector<int64_t>& phi)
{
  vector<char> sieve(segment_size);
  vector<int32_t> counters(segment_size);
  vector<int64_t> next;
  phi.resize(primes.size(), 0);
  mu_sum.resize(primes.size(), 0);
  next.reserve(primes.size());
  next.push_back(0);

  int64_t start_idx = segments_per_thread * thread_num;
  int64_t stop_idx = min(segments_per_thread * (thread_num + 1), segments);
  int64_t low_thread = segment_size * start_idx + 1;
  int64_t S2_thread = 0;

  // Initialize next multiples
  for (size_t j = 1; j < primes.size(); j++)
  {
    int64_t prime = primes[j];
    int64_t next_multiple = ((low_thread + prime - 1) / prime) * prime;
    next.push_back(next_multiple);
  }

  // Process the segments corresponding to the current thread
  for (int64_t j = start_idx; j < stop_idx; j++)
  {
    fill(sieve.begin(), sieve.end(), 1);

    // Current segment = interval [low, high[
    int64_t low = segment_size * j + 1;
    int64_t high = min(low + segment_size, limit);
    int64_t b = 1;

    for (; b <= c; b++)
    {
      int64_t k = next[b];
      for (int64_t prime = primes[b]; k < high; k += prime)
        sieve[k - low] = 0;
      next[b] = k;
    }

    // Initialize special tree data structure from sieve
    cnt_finit(sieve, counters, segment_size);

    // For c + 1 <= b < pi_y
    // Find all special leaves: n = primes[b] * m, with mu[m] != 0 and primes[b] < lpf[m]
    // Such that: low <= x / n < high
    for (; b < pi_y; b++)
    {
      int64_t prime = primes[b];
      int64_t m_min = max(x / (prime * high), y / prime);
      int64_t m_max = min(x / (prime * low), y);

      if (prime >= m_max)
        break;

      for (int64_t m = m_max; m > m_min; m--)
      {
        if (mu[m] != 0 && prime < lpf[m])
        {
          int64_t n = prime * m;
          int64_t count = cnt_query(counters, (x / n) - low);
          int64_t phi_xn = phi[b] + count;
          S2_thread -= mu[m] * phi_xn;
          mu_sum[b] -= mu[m];
        }
      }

      phi[b] += cnt_query(counters, (high - 1) - low);
      cross_off(prime, low, high, next[b], sieve, counters);
    }
  }

  return S2_thread;
}

/// Calculate the contribution of the special leaves.
/// This is a parallel implementation without load balancing.
/// @pre y > 0 && c > 1
///
int64_t S2(int64_t x,
           int64_t y,
           int64_t pi_y,
           int64_t c,
           vector<int32_t>& primes,
           vector<int32_t>& lpf,
           vector<int32_t>& mu,
           int threads)
{
  int64_t S2_total = 0;
  int64_t limit = x / y + 1;
  int64_t segment_size = next_power_of_2(isqrt(limit));
  int64_t segments = (limit + segment_size - 1) / segment_size;
  threads = in_between(1, threads, segments);
  int64_t segments_per_thread = (segments + threads - 1) / threads;

  vector<vector<int64_t> > phi(threads);
  vector<vector<int64_t> > mu_sum(threads);

  #pragma omp parallel for num_threads(threads) reduction(+: S2_total)
  for (int i = 0; i < threads; i++)
  {
    S2_total += S2_thread(x, y, pi_y, c, limit, segments,
        segment_size, segments_per_thread, i, primes, lpf, mu,
            mu_sum[i], phi[i]);
  }

  // Once all threads have finished reconstruct and add the
  // missing contribution of all special leaves. This must
  // be done in order as each thread (i) requires the sum of
  // the phi values from the previous threads.
  //
  for (int i = 1; i < threads; i++)
  {
    for (size_t j = 0; j < phi[i].size(); j++)
    {
      S2_total += phi[i - 1][j] * mu_sum[i][j];
      phi[i][j] += phi[i - 1][j];
    }
  }

  return S2_total;
}

} // namespace

#endif /* _OPENMP */

namespace primecount {

/// Calculate the number of primes below x using the
/// Lagarias-Miller-Odlyzko algorithm.
/// Run time: O(x^(2/3)) operations, O(x^(1/3) * log log x) space.
///
int64_t pi_lmo_parallel1(int64_t x, int threads)
{
#ifdef _OPENMP

  if (x < 2)
    return 0;

  double beta = 0.6;
  double alpha = max(1.0, log(log((double) x)) * beta);
  int64_t x13 = iroot<3>(x);
  int64_t y = (int64_t)(x13 * alpha);

  vector<int32_t> mu = make_moebius(y);
  vector<int32_t> lpf = make_least_prime_factor(y);
  vector<int32_t> primes;
  primes.push_back(0);
  primesieve::generate_primes(y, &primes);

  int64_t pi_y = primes.size() - 1;
  int64_t c = min(PhiTiny::MAX_A, pi_y);
  int64_t s1, s2, p2;

  threads = get_omp_threads(threads);
  omp_set_nested(true);

  #pragma omp parallel sections num_threads(threads)
  {
    #pragma omp section
    s2 = S2(x, y, pi_y, c, primes, lpf , mu, threads - 1);
    #pragma omp section
    {
      s1 = S1(x, y, c, primes, lpf , mu);
      p2 = P2(x, y);
    }
  }
  omp_set_nested(false);

  int64_t phi = s1 + s2;
  int64_t sum = phi + pi_y - 1 - p2;

  return sum;
#else
  return pi_lmo4(x);
#endif
}

} // namespace primecount