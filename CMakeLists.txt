set(CMAKE_MINIMUM_REQUIRED_VERSION 3.25)
cmake_minimum_required(VERSION 3.25)
enable_language(CXX)
set(CMAKE_CXX_STANDARD 20)

find_package(PkgConfig REQUIRED)
    
set(OMNIMEM_BACKEND_DPDK ON CACHE BOOL "Use DPDK as memory backend")
if (${OMNIMEM_BACKEND_DPDK})
    add_compile_definitions(OMNIMEM_BACKEND_DPDK)
endif()
if (${OMNIMEM_BACKEND_DPDK})
    pkg_search_module(DPDK REQUIRED libdpdk IMPORTED_TARGET GLOBAL)
endif()
pkg_search_module(JSONCPP REQUIRED jsoncpp IMPORTED_TARGET GLOBAL)

include(FetchContent)
FetchContent_Declare(
  cli11
  GIT_REPOSITORY git@github.com:CLIUtils/CLI11
  GIT_TAG        v2.2.0
)
FetchContent_MakeAvailable(cli11)

project(OmniStack)

add_compile_options(-g)

set(OMNISTACK_EXEC omnistack)
set(OMNISTACK_USERLIB omniuser)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_subdirectory(tool)
add_subdirectory(src)
add_subdirectory(application)
add_subdirectory(tests)
