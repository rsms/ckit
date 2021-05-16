if(NOT _CKIT_LIB_INCLUDED)
set(_CKIT_LIB_INCLUDED 1)

cmake_policy(SET CMP0057 NEW) # "IN LIST" operator

set(CKIT_DIR ${CMAKE_CURRENT_LIST_DIR})
set(CKIT_RBASE_DIR ${CKIT_DIR}/pkg/rbase)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_C_STANDARD 11)

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
  message(VERBOSE "ckit_gen_sources_list ${target}")
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
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${filename_prefix}.ckit-sources.txt" ${_dep_sources_list})
endfunction()


macro(add_executable target)
  _add_executable(${target} ${ARGN})
  ckit_gen_sources_list(${target} ${target})
endmacro()


macro(add_library target)
  _add_library(${target} ${ARGN})
  ckit_gen_sources_list(${target} ${target})
endmacro()


macro(ckit_set_test target)
  if(R_BUILD_TESTING_THIS_PROJECT)
    add_custom_target(test COMMAND ${target} DEPENDS ${target} USES_TERMINAL)
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

    add_executable(${target}-test ${CKIT_RBASE_DIR}/testing.c ${_sources})
    target_compile_definitions(${target}-test PRIVATE R_TESTING_ENABLED=1 R_TESTING_MAIN_IMPL=1)

    get_target_property(_include_directories ${target} INCLUDE_DIRECTORIES)
    if (NOT (_include_directories STREQUAL "_include_directories-NOTFOUND"))
      target_include_directories(${target}-test PRIVATE ${_include_directories})
    endif()

    get_target_property(_compile_options ${target} COMPILE_OPTIONS)
    if (NOT (_compile_options STREQUAL "_compile_options-NOTFOUND"))
      target_compile_options(${target}-test PRIVATE ${_compile_options})
    endif()

    # if (_main_source_file)
    #   target_sources(${target}-test PRIVATE ${_main_source_file})
    #   # Note: R_TESTING_INIT_IMPL makes rbase/testing.c define an init/constructor function
    #   target_compile_definitions(${target}-test PRIVATE R_TESTING_INIT_IMPL=1)
    # else()
    #   # Note: R_TESTING_MAIN_IMPL makes rbase/testing.c define a main function
    #   # set_source_files_properties(${CKIT_RBASE_DIR}/testing.c PROPERTIES COMPILE_FLAGS
    #   #   R_TESTING_MAIN_IMPL=1)
    # endif()

    if (NOT (target STREQUAL "rbase"))
      target_link_libraries(${target}-test rbase)
    endif()
    if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
      ckit_set_test(${target}-test)
    endif()
  endif()
endmacro()


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
    add_compile_definitions(R_TESTING_ENABLED=1)
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
      -ffile-prefix-map=${CKIT_DIR}/=$CKIT_DIR/
      -ffile-prefix-map=../../=./
      # -ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}/=${SRC_FILENAME_PREFIX}/
      -fstrict-aliasing
      -fcolor-diagnostics
    )
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
  else()
    add_compile_definitions(NDEBUG=1)  # disable assertions
    add_compile_options(-O3)
    add_link_options(-dead_strip)
  endif()

  # enable LTO aka IPO if available
  include(CheckIPOSupported)
  check_ipo_supported(RESULT IPO_SUPPORTED)
  if(IPO_SUPPORTED)
    # set_target_properties(rbase PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
    set_property(GLOBAL PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()

endfunction() # _ckit_configure_project

endif() # _CKIT_LIB_INCLUDED
