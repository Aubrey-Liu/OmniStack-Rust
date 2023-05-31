set(CMAKE_MINIMUM_REQUIRED_VERSION 3.25)
enable_language(CXX)
set(CMAKE_CXX_STANDARD 20)

project(OmniStack)

include_directories(include)

add_subdirectory(library)
add_subdirectory(tool)



add_subdirectory(application)
