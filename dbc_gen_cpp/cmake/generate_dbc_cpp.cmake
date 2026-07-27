# SPDX-FileCopyrightText: 2026 Polymath Robotics, Inc.
# SPDX-License-Identifier: Apache-2.0
function(generate_dbc_cpp library_name)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)

  set(one_value_args DBC)
  cmake_parse_arguments(ARG "" "${one_value_args}" "" ${ARGN})
  if(NOT ARG_DBC)
    message(FATAL_ERROR "generate_dbc_cpp: Missing required keyword argument DBC")
  endif()

  set(gen_basedir ${CMAKE_CURRENT_BINARY_DIR}/dbc_gen_cpp)
  set(gen_dir ${gen_basedir}/${library_name})

  set(generated_c ${gen_dir}/${library_name}.c)
  set(generated_h ${gen_dir}/${library_name}.h)
  set(generated_hpp ${gen_dir}/${library_name}.hpp)
  set(generated_files ${generated_c} ${generated_h} ${generated_hpp})

  # Kind of awkward, but makes the sources get regenerated if the generator tool changes, by depending on the generator sources.
  # Finds the python module directory via Python3 import
  execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import dbc_gen_cpp, os; print(os.path.dirname(dbc_gen_cpp.__file__))"
    OUTPUT_VARIABLE GENERATOR_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  file(GLOB_RECURSE GENERATOR_SOURCES CONFIGURE_DEPENDS
    "${GENERATOR_DIR}/*.py"
    "${GENERATOR_DIR}/templates/*.j2"
  )

  # Generate the source files
  add_custom_command(
    OUTPUT ${generated_files}
    COMMAND ${Python3_EXECUTABLE} -m dbc_gen_cpp ${ARG_DBC} -o ${gen_dir} -n ${library_name}
    DEPENDS ${ARG_DBC} ${GENERATOR_SOURCES}
    COMMENT "Generating C source from DBC ${ARG_DBC} with cantools"
    VERBATIM
  )
  add_custom_target(${library_name}_c_sources
    DEPENDS ${generated_c} ${generated_h}
  )

  # Create the library target from generated sources
  add_library(${library_name} STATIC ${generated_c})
  add_dependencies(${library_name} ${library_name}_c_sources)
  target_link_libraries(${library_name} PUBLIC dbc_gen_cpp::dbc_gen_cpp)
  target_include_directories(${library_name}
    PUBLIC ${gen_basedir}
  )

  # Install the generated files to the install space, just in case they're included in public headers
  install(
    FILES ${generated_h} ${generated_hpp}
    DESTINATION include/${library_name}
  )
endfunction()
