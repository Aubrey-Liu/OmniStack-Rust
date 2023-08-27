add_library(omni_common 
STATIC 
    include/omnistack/common/constant.hpp 
    include/omnistack/common/protocol_headers.hpp
    include/omnistack/common/cpu.hpp
    include/omnistack/common/hash.hpp
    include/omnistack/common/random.hpp
    include/omnistack/common/thread.hpp
    include/omnistack/common/time.hpp
    include/omnistack/common/dynamic_link.hpp
    config.cpp
)
set_target_properties(omni_common PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(omni_common PUBLIC include)
target_link_libraries(omni_common PRIVATE omni_memory PkgConfig::JSONCPP PUBLIC pthread dl)
set(OMNI_LIBRARIES ${OMNI_LIBRARIES} omni_common PARENT_SCOPE)