# SPDX-FileCopyrightText: 2026 Polymath Robotics, Inc.
# SPDX-License-Identifier: Apache-2.0

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

  find_package(ament_cmake_test REQUIRED)
  find_package(ament_cmake_pytest REQUIRED)
  find_package(Catch2 REQUIRED)

  # Python-level tests for the generator (e.g. the fail-on-ambiguous-name path,
  # which cannot be exercised through a generated header).
  ament_add_pytest_test(test_enum_generation
    test/test_enum_generation.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  )

  generate_dbc_cpp(fake_vehicle_can
    DBC ${CMAKE_CURRENT_SOURCE_DIR}/test/FakeVehicle.dbc
  )

  generate_dbc_cpp(fake_j1939_can
    DBC ${CMAKE_CURRENT_SOURCE_DIR}/test/FakeJ1939.dbc
  )

  # Build the Catch2-based unit test and register it with ament.
  add_executable(test_dbc_cpp test/test_dbc_cpp.cpp)
  target_link_libraries(test_dbc_cpp
    Catch2::Catch2WithMain
    fake_vehicle_can
    fake_j1939_can
    dbc_gen_cpp::dbc_gen_cpp
  )
  target_include_directories(test_dbc_cpp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test)

  ament_add_test(
    test_dbc_cpp
    GENERATE_RESULT_FOR_RETURN_CODE_ZERO
    COMMAND "$<TARGET_FILE:test_dbc_cpp>"
      -r junit
      -s
      -o test_results/${PROJECT_NAME}/test_dbc_cpp_output.xml
    ENV CATCH_CONFIG_CONSOLE_WIDTH=120
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )

  # Verify that generate_dbc_cpp() installs the generated headers into the export
  # location (include/<library_name>/) so downstream packages can consume them.
  # This exercises the install/export path, which the compiled test above does not:
  # that test links the libraries in-tree via their BUILD_INTERFACE include dirs.
  ament_add_test(
    test_dbc_exported_headers
    GENERATE_RESULT_FOR_RETURN_CODE_ZERO
    COMMAND ${CMAKE_COMMAND}
      -DINSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
      "-DLIBRARIES=fake_vehicle_can;fake_j1939_can"
      -P ${CMAKE_CURRENT_SOURCE_DIR}/test/check_exported_headers.cmake
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  )
endif()

ament_package()
