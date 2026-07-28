// SPDX-FileCopyrightText: 2026 Polymath Robotics, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Include the appropriate Catch2 header depending on whether Catch2 v3 or v2
// is available, and make Catch::Approx available under the unqualified name
// `Approx` used throughout the tests.
#if __has_include(<catch2/catch_all.hpp>)
  #include <catch2/catch_all.hpp>
  #include <catch2/catch_approx.hpp>
using Catch::Approx;
#elif __has_include(<catch2/catch.hpp>)
  #include <catch2/catch.hpp>
#else
  #error "Catch2 headers not found. Please install Catch2 (v2 or v3)."
#endif
