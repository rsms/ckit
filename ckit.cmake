include_guard()
if(__CKIT__)
  return()
endif()
set(__CKIT__ TRUE)
set(ENV{CKIT_DIR} ${CMAKE_CURRENT_LIST_DIR})
message(STATUS "using ckit at ${CMAKE_CURRENT_LIST_DIR}")

cmake_policy(SET CMP0057 NEW) # "IN LIST" operator

set(CKIT_DIR ${CMAKE_CURRENT_LIST_DIR})
set(CKIT_RBASE_DIR ${CKIT_DIR}/pkg/rbase)

# Set default langauge standards.
# Note that we have to use add_compile_options instead of setting CMAKE_CXX_STANDARD
# and CMAKE_C_STANDARD as the latter is appended to the end of compiler flags at
# build time while add_compile_options is added to the beginning, making it possible
# for projects to override.
# set(CMAKE_CXX_STANDARD 14)
# set(CMAKE_C_STANDARD 11)
add_compile_options(
  $<$<COMPILE_LANGUAGE:C>:-std=c11>
  $<$<COMPILE_LANGUAGE:OBJC>:-std=c11>
  $<$<COMPILE_LANGUAGE:CXX>:-std=c++14>
  $<$<COMPILE_LANGUAGE:OBJCXX>:-std=c++14>
)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# get_filename_component(R_ROOT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)

# macro(add_executable)
#   message("add it!")
# endmacro()

function(target_link_libraries _target)
  set(_mode "PUBLIC")
  foreach(_arg IN LISTS ARGN)
    if (_arg MATCHES "INTERFACE|PUBLIC|PRIVATE|LINK_PRIVATE|LINK_PUBLIC|LINK_INTERFACE_LIBRARIES")
      set(_mode "${_arg}")
    else()
      if (NOT _arg MATCHES "debug|optimized|general")
        set_property(GLOBAL APPEND PROPERTY GlobalTargetDepends${_target} ${_arg})
      endif()
    endif()
  endforeach()
  _target_link_libraries(${_target} ${ARGN})
endfunction()

function(ckit_get_link_dependencies _target _listvar)
  set(_worklist ${${_listvar}})
  if (TARGET ${_target})
    list(APPEND _worklist ${_target})
    get_property(_dependencies GLOBAL PROPERTY GlobalTargetDepends${_target})
    foreach(_dependency IN LISTS _dependencies)
      if (NOT _dependency IN_LIST _worklist)
        ckit_get_link_dependencies(${_dependency} _worklist)
      endif()
    endforeach()
    set(${_listvar} "${_worklist}" PARENT_SCOPE)
  endif()
endfunction()

# ckit_gen_sources_list(target) writes a file with all sources needed for a target to
# ${CMAKE_CURRENT_BINARY_DIR}/${filename_prefix}.ckit-sources.txt
function(ckit_gen_sources_list target filename_prefix)
  set(_output_file ${CMAKE_CURRENT_BINARY_DIR}/${filename_prefix}.ckit-sources.txt)
  message(VERBOSE "ckit_gen_sources_list ${target} -> ${_output_file}")
  ckit_get_link_dependencies(${target} _deps)
  set(_dep_sources_list "")
  foreach(_dep IN LISTS _deps)
    # get_target_property(_target_type ${_dep} TYPE)
    # message("${_dep} _target_type=${_target_type}")
    get_target_property(_srcs ${_dep} SOURCES)
    get_target_property(_src_dir ${_dep} SOURCE_DIR)
    if(NOT (_srcs STREQUAL "_srcs-NOTFOUND"))
      foreach(_src IN LISTS _srcs)
        if(NOT (IS_ABSOLUTE "${_src}"))
          set(_src "${_src_dir}/${_src}")
        endif()
        # message("${_src}")
        set(_dep_sources_list "${_dep_sources_list}${_src}\n")
      endforeach()
    endif()
  endforeach()
  file(WRITE "${_output_file}" ${_dep_sources_list})
endfunction()


# ckit_target_post_dsymutil adds a POST_BUILD command that runs dsymutil on the executable.
# This is needed on macos to embed complete debug info for stack traces.
macro(ckit_target_post_dsymutil target)
  if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND dsymutil ${CMAKE_CURRENT_BINARY_DIR}/${target}
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "dsymutil ${target}"
    )
  endif()
endmacro()


macro(add_executable target)
  _add_executable(${target} ${ARGN})
  ckit_gen_sources_list(${target} ${target})
  ckit_target_post_dsymutil(${target})
endmacro()


macro(add_library target)
  _add_library(${target} ${ARGN})
  ckit_gen_sources_list(${target} ${target})
endmacro()


macro(ckit_set_test target)
  if(R_BUILD_TESTING_THIS_PROJECT)
    add_custom_target(test
      DEPENDS ${target}
      COMMAND ${CMAKE_CURRENT_BINARY_DIR}/${target}
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      USES_TERMINAL)
    ckit_gen_sources_list(${target} test)
  endif()
endmacro()


macro(ckit_require_package pkg)
  list(FIND R_INCLUDED_PACKAGES ${pkg} _index)
  if (${_index} LESS 0)
    list(APPEND R_INCLUDED_PACKAGES ${pkg})
    # get_filename_component(PARENT_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR} DIRECTORY)
    # add_subdirectory("../rbase" ${PARENT_BINARY_DIR}/rbase EXCLUDE_FROM_ALL)
    add_subdirectory(${CKIT_DIR}/pkg/${pkg} ${pkg} EXCLUDE_FROM_ALL)
  endif()
endmacro()


# _ckit_define_project_test()
# defines project-wide "test" target if needed
macro(_ckit_define_project_test)
  if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND (NOT TARGET test))
    add_custom_target(test)
  endif()
endmacro()


# ckit_add_test_dependencies(target...)
macro(ckit_add_test_dependencies)
  _ckit_define_project_test()
  if (TARGET test)
    add_dependencies(test ${ARGN})
  endif()
endmacro()


# ckit_add_test(TEST_TARGET EXE_TARGET CMDARGS...)
# Adds a command target TEST_TARGET to run the executable EXE_TARGET.
# TEST_TARGET is added as a dependency to the project-wide "test" target.
macro(ckit_add_test TEST_TARGET EXE_TARGET)
  target_compile_definitions(${EXE_TARGET} PRIVATE R_TESTING_ENABLED=1)
  add_custom_target(${TEST_TARGET}
    DEPENDS ${EXE_TARGET}
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/${EXE_TARGET} ${ARGN}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    USES_TERMINAL)
  ckit_gen_sources_list(${EXE_TARGET} test-${TEST_TARGET})
  ckit_add_test_dependencies(test ${TEST_TARGET})
endmacro()


# ckit_force_load_libfile(TARGET LIBFILENAME)
# Make sure that all symbols of static library file LIBFILENAME are loaded into TARGET
macro(ckit_force_load_libfile TARGET LIBFILENAME)
  if (CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_link_options(${TARGET} PRIVATE
      -Wl,-force_load,${LIBFILENAME}
    )
  elseif (CMAKE_C_COMPILER_ID MATCHES "GNU")
    target_link_options(${TARGET} PRIVATE
      -Wl,--whole-archive ${LIBFILENAME} -Wl,--no-whole-archive
    )
  else()
    message(WARNING "ckit_force_load_libfile: unsupported compiler ${CMAKE_C_COMPILER_ID}")
  endif()
endmacro()


# ckit_force_load_lib(TARGET LIBNAME)
# Make sure that all symbols of static library LIBNAME are loaded into TARGET
macro(ckit_force_load_lib TARGET LIBNAME)
  ckit_force_load_libfile(${TARGET} ${CMAKE_CURRENT_BINARY_DIR}/lib${LIBNAME}.a)
endmacro()


# ckit_define_test(target [main_source_file])
macro(ckit_define_test target)
  set(extra_macro_args ${ARGN})
  list(LENGTH extra_macro_args num_extra_args)
  unset(_main_source_file)
  if (${num_extra_args} GREATER 0)
    list(GET extra_macro_args 0 _main_source_file)
  endif()

  if(R_BUILD_TESTING_THIS_PROJECT AND TEST_ENABLED)
    get_target_property(_sources ${target} SOURCES)

    # if there's a main function, exclude that file from sources to
    # avoid clash with main() in rbase/testing.c
    if (_main_source_file)
      message(STATUS "ckit testing: excluding ${_main_source_file} containing a main() function")
      list(REMOVE_ITEM _sources ${_main_source_file})
    endif()

    set(_test_exe_target ${target}-test-exe)
    set(_test_run_target ${target}-test)

    add_executable(${_test_exe_target} ${CKIT_RBASE_DIR}/testing.c ${_sources})
    target_compile_definitions(${_test_exe_target} PRIVATE R_TESTING_ENABLED=1 R_TESTING_MAIN_IMPL=1)

    get_target_property(_include_directories ${target} INCLUDE_DIRECTORIES)
    if (NOT (_include_directories STREQUAL "_include_directories-NOTFOUND"))
      target_include_directories(${_test_exe_target} PRIVATE ${_include_directories})
    endif()

    get_target_property(_compile_definitions ${target} COMPILE_DEFINITIONS)
    if (NOT (_compile_definitions STREQUAL "_compile_definitions-NOTFOUND"))
      target_compile_definitions(${_test_exe_target} PRIVATE ${_compile_definitions})
    endif()

    get_target_property(_compile_options ${target} COMPILE_OPTIONS)
    if (NOT (_compile_options STREQUAL "_compile_options-NOTFOUND"))
      target_compile_options(${_test_exe_target} PRIVATE ${_compile_options})
    endif()

    ckit_get_link_dependencies(${target} _deps)
    foreach(_dep IN LISTS _deps)
      if (NOT (${target} STREQUAL "${_dep}"))
        target_link_libraries(${_test_exe_target} ${_dep})
      endif()
    endforeach()

    target_compile_options(${_test_exe_target} PRIVATE -gdwarf)

    # if (_main_source_file)
    #   target_sources(${_test_exe_target} PRIVATE ${_main_source_file})
    #   # Note: R_TESTING_INIT_IMPL makes rbase/testing.c define an init/constructor function
    #   target_compile_definitions(${_test_exe_target} PRIVATE R_TESTING_INIT_IMPL=1)
    # else()
    #   # Note: R_TESTING_MAIN_IMPL makes rbase/testing.c define a main function
    #   # set_source_files_properties(${CKIT_RBASE_DIR}/testing.c PROPERTIES COMPILE_FLAGS
    #   #   R_TESTING_MAIN_IMPL=1)
    # endif()

    if (NOT (target STREQUAL "rbase"))
      target_link_libraries(${_test_exe_target} rbase)
    endif()

    # define project-wide "test" target if needed
    # if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    #   add_custom_target(test COMMAND touch ${CMAKE_CURRENT_BINARY_DIR}/test.txt)
    #   ckit_gen_sources_list(${target}-test test) # FIXME
    #   #ckit_set_test(${target}-test)
    # endif()

    # define ${target}-test which builds and runs the test
    add_custom_target(${_test_run_target}
      DEPENDS ${_test_exe_target}
      COMMAND ${CMAKE_CURRENT_BINARY_DIR}/${_test_exe_target}
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      USES_TERMINAL)

    ckit_gen_sources_list(${_test_exe_target} test-${target})

    # add_dependencies(test ${_test_run_target})
    ckit_add_test_dependencies(${_test_run_target})

  endif()
endmacro() # ckit_define_test


# ckit_configure_project(lang...) configures the current project to rsms standards.
# It takes a list of language names (valid: C, C++)
macro(ckit_configure_project)
  # message("CMAKE_PROJECT_NAME: ${CMAKE_PROJECT_NAME}")
  # message("PROJECT_NAME: ${PROJECT_NAME}")
  unset(R_BUILD_TESTING_THIS_PROJECT)
  if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    #message("enable testing for ${PROJECT_NAME}")
    # # set_property(GLOBAL PROPERTY USE_FOLDERS ON) # support folders in IDEs
    # include(CTest)
    # if(BUILD_TESTING)
    #   set(R_BUILD_TESTING_THIS_PROJECT ON)
    #   add_compile_definitions(R_TESTING_ENABLED=1)
    # endif()
    set(R_BUILD_TESTING_THIS_PROJECT ON)
    # if (${CMAKE_BUILD_TYPE} MATCHES "Debug")
    if (TEST_ENABLED)
      add_compile_definitions(R_TESTING_ENABLED=1)
    endif()
  endif()
  _ckit_configure_project(${ARGN})
endmacro()

function(_ckit_configure_project)
  # foreach(lang IN LISTS ARGN)
  #   if (lang STREQUAL "C")
  #     # set(CMAKE_C_STANDARD 17) # not supported by cmake <=3.18
  #     set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c17")
  #   elseif ((lang STREQUAL "C++") OR (lang STREQUAL "CXX"))
  #     set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c++17")
  #   else()
  #     message(WARNING "ignoring unexpected language \"${lang}\"")
  #   endif()
  # endforeach()

  get_filename_component(SRC_FILENAME_PREFIX ${CMAKE_SOURCE_DIR} NAME)
  add_compile_options(
    -Wall -Wextra
    -Wvla
    -Wimplicit-fallthrough
    -Wno-missing-field-initializers
    -Wno-unused-parameter
    -Werror=implicit-function-declaration
    -Wunused
    -I${CKIT_DIR}/pkg
  )

  # if (APPLE)
  #   add_compile_options(-mmacosx-version-min=10.10)
  # endif()

  if ((CMAKE_C_COMPILER_ID MATCHES "Clang") OR (CMAKE_C_COMPILER_ID MATCHES "GNU"))
    add_compile_options(
      -fstrict-aliasing
      -fcolor-diagnostics
    )
    if (${CMAKE_BUILD_TYPE} MATCHES "Debug")
      # full source filename paths in debug builds
      add_compile_options(
        -ffile-prefix-map=../../=${CMAKE_CURRENT_SOURCE_DIR}/
      )
    else()
      # short symbolic source filename paths in release builds
      add_compile_options(
        -ffile-prefix-map=${CKIT_DIR}/=<ckit>/
        -ffile-prefix-map=../../=<${PROJECT_NAME}>/
        -ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=<${PROJECT_NAME}>/
      )
    endif()
  endif()

  if (CMAKE_C_COMPILER_ID MATCHES "Clang")
    add_compile_options(
      -Wno-nullability-completeness
      -Wno-nullability-inferred-on-nested-type
      -fdiagnostics-absolute-paths
    )
  endif()

  if (${CMAKE_BUILD_TYPE} MATCHES "Debug")
    set_property(GLOBAL PROPERTY LINK_WHAT_YOU_USE on)
    add_compile_definitions(DEBUG=1)
    add_compile_options(-O0)
  elseif (${CMAKE_BUILD_TYPE} MATCHES "Release")
    if (${CMAKE_BUILD_TYPE} MATCHES "Safe")
      remove_definitions(-DNDEBUG)  # enable assertions
    else()
      add_compile_definitions(NDEBUG=1)  # disable assertions
    endif()
    add_compile_options(-O3)
    add_link_options(-dead_strip)
    if (CMAKE_C_COMPILER_ID MATCHES "Clang")
      add_link_options(-flto)
    else()
      # enable LTO/IPO if available
      include(CheckIPOSupported)
      check_ipo_supported(RESULT IPO_SUPPORTED)
      if(IPO_SUPPORTED)
        set_property(GLOBAL PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
      endif()
    endif()
  endif()



endfunction() # _ckit_configure_project
