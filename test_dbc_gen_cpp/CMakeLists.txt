# SPDX-FileCopyrightText: 2026 Polymath Robotics, Inc.
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025-present Polymath Robotics, Inc. All rights reserved
# Proprietary. Any unauthorized copying, distribution, or modification of this software is strictly prohibited.
cmake_minimum_required(VERSION 3.22)
project(test_dbc_gen_cpp)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
  add_link_options(-Wl,-no-undefined)
endif()

find_package(ament_cmake_auto REQUIRED)

if(BUILD_TESTING)
  ament_auto_find_test_dependencies()

  generate_dbc_cpp(fake_vehicle_can
    DBC ${CMAKE_CURRENT_SOURCE_DIR}/test/FakeVehicle.dbc
  )

  generate_dbc_cpp(fake_j1939_can
    DBC ${CMAKE_CURRENT_SOURCE_DIR}/test/FakeJ1939.dbc
  )

  polymath_add_catch2_test(test_dbc_cpp test/test_dbc_cpp.cpp "fake_vehicle_can;fake_j1939_can" dbc_gen_cpp)
endif()

ament_package()
