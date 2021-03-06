///
/// @file   prev_power_of_2.cpp
/// @brief  Round down to nearest power of 2.
///
/// Copyright (C) 2017 Kim Walisch, <kim.walisch@gmail.com>
///
/// This file is distributed under the BSD License. See the COPYING
/// file in the top level directory.
///

#include <imath.hpp>

#include <stdint.h>
#include <iostream>
#include <cmath>
#include <cstdlib>

using namespace std;

void check(bool OK)
{
  cout << "   " << (OK ? "OK" : "ERROR") << "\n";
  if (!OK)
    exit(1);
}

int main()
{
  uint64_t n;
  uint64_t res1;
  uint64_t res2;

  for (n = 1; n < 100000; n++)
  {
    res1 = prev_power_of_2(n);
    res2 = 1ull << (uint64_t) log2(n);
    cout << "prev_power_of_2(" << n << ") = " << res1;
    check(res1 == res2);
  }

  n = (1ull << 32) - 1;
  res1 = prev_power_of_2(n);
  res2 = 1ull << (uint64_t) log2(n);
  cout << "prev_power_of_2(" << n << ") = " << res1;
  check(res1 == res2);

  n = 1ull << 32;
  res1 = prev_power_of_2(n);
  res2 = 1ull << (uint64_t) log2(n);
  cout << "prev_power_of_2(" << n << ") = " << res1;
  check(res1 == res2);

  n = (1ull << 63) - 1;
  res1 = prev_power_of_2(n);
  cout << "prev_power_of_2(" << n << ") = " << res1;
  check(res1 == (1ull << 62));

  n = 1ull << 63;
  res1 = prev_power_of_2(n);
  cout << "prev_power_of_2(" << n << ") = " << res1;
  check(res1 == (1ull << 63));

  n = 18446744073709551615ull;
  res1 = prev_power_of_2(n);
  cout << "prev_power_of_2(" << n << ") = " << res1;
  check(res1 == (1ull << 63));

  cout << endl;
  cout << "All tests passed successfully!" << endl;

  return 0;
}
