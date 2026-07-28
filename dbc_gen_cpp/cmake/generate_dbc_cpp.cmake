# SPDX-FileCopyrightText: 2026 Polymath Robotics, Inc.
# SPDX-License-Identifier: Apache-2.0
#
# generate_dbc_cpp(<library_name> DBC <path/to/file.dbc>)
#
# Generates C/C++ CAN message types from a DBC file and builds them into a static
# library named <library_name> (C++ namespace and C-symbol prefix also <library_name>,
# headers at include/<library_name>/<library_name>.hpp).
#
# The library is ALWAYS installed and exported, so a downstream package that does
# find_package(<this_package>) can link it as the imported target
# <this_package>::<library_name>. Within the generating package it is also usable by
# its plain local name.
#
# NOTE: this is a macro, not a function, on purpose. It calls ament_export_targets()
# and ament_export_dependencies(), whose bookkeeping is stored in ordinary variables
# in the current scope; ament_package() reads them from the package's top-level scope,
# so they must not be trapped inside a function scope.
macro(generate_dbc_cpp library_name)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  cmake_parse_arguments(_dbc_arg "" "DBC" "" ${ARGN})
  if(NOT _dbc_arg_DBC)
    message(FATAL_ERROR "generate_dbc_cpp: Missing required keyword argument DBC")
  endif()

  set(_dbc_gen_basedir ${CMAKE_CURRENT_BINARY_DIR}/dbc_gen_cpp)
  set(_dbc_gen_dir ${_dbc_gen_basedir}/${library_name})

  set(_dbc_generated_c ${_dbc_gen_dir}/${library_name}.c)
  set(_dbc_generated_h ${_dbc_gen_dir}/${library_name}.h)
  set(_dbc_generated_hpp ${_dbc_gen_dir}/${library_name}.hpp)

  # Kind of awkward, but makes the sources get regenerated if the generator tool
  # changes, by depending on the generator sources. Finds the python module directory.
  execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import dbc_gen_cpp, os; print(os.path.dirname(dbc_gen_cpp.__file__))"
    OUTPUT_VARIABLE _dbc_generator_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  file(GLOB_RECURSE _dbc_generator_sources CONFIGURE_DEPENDS
    "${_dbc_generator_dir}/*.py"
    "${_dbc_generator_dir}/templates/*.j2"
  )

  # Generate the source files.
  add_custom_command(
    OUTPUT ${_dbc_generated_c} ${_dbc_generated_h} ${_dbc_generated_hpp}
    COMMAND ${Python3_EXECUTABLE} -m dbc_gen_cpp ${_dbc_arg_DBC} -o ${_dbc_gen_dir} -n ${library_name}
    DEPENDS ${_dbc_arg_DBC} ${_dbc_generator_sources}
    COMMENT "Generating C source from DBC ${_dbc_arg_DBC} with cantools"
    VERBATIM
  )
  add_custom_target(${library_name}_c_sources
    DEPENDS ${_dbc_generated_c} ${_dbc_generated_h}
  )

  # Create the library target from generated sources.
  add_library(${library_name} STATIC ${_dbc_generated_c})
  add_dependencies(${library_name} ${library_name}_c_sources)
  target_link_libraries(${library_name} PUBLIC dbc_gen_cpp::dbc_gen_cpp)
  target_include_directories(${library_name}
    PUBLIC
      $<BUILD_INTERFACE:${_dbc_gen_basedir}>
      $<INSTALL_INTERFACE:include>
  )

  # Install + export the target and its headers so other packages can link it as
  # <this_package>::${library_name}.
  install(
    TARGETS ${library_name}
    EXPORT ${library_name}Targets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
  )
  install(
    FILES ${_dbc_generated_h} ${_dbc_generated_hpp}
    DESTINATION include/${library_name}
  )
  ament_export_targets(${library_name}Targets HAS_LIBRARY_TARGET)
  ament_export_dependencies(dbc_gen_cpp)
endmacro()
