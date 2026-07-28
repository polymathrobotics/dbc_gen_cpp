# SPDX-FileCopyrightText: 2026 Polymath Robotics, Inc.
# SPDX-License-Identifier: Apache-2.0
#
# Verifies that generate_dbc_cpp() exports the generated headers into the install
# space so downstream packages can include them as <library_name>/<library_name>.hpp.
#
# Invoked with -DINSTALL_PREFIX=<prefix> -DLIBRARIES=<lib1;lib2>.

if(NOT DEFINED INSTALL_PREFIX)
  message(FATAL_ERROR "check_exported_headers: INSTALL_PREFIX not set")
endif()
if(NOT DEFINED LIBRARIES)
  message(FATAL_ERROR "check_exported_headers: LIBRARIES not set")
endif()

set(_missing "")
foreach(_lib IN LISTS LIBRARIES)
  foreach(_ext h hpp)
    set(_header "${INSTALL_PREFIX}/include/${_lib}/${_lib}.${_ext}")
    if(EXISTS "${_header}")
      message(STATUS "Found exported header: ${_header}")
    else()
      message(WARNING "Missing exported header: ${_header}")
      list(APPEND _missing "${_header}")
    endif()
  endforeach()
endforeach()

if(_missing)
  message(FATAL_ERROR "generate_dbc_cpp did not export the expected headers:\n  ${_missing}")
endif()
