//
// Created by liuhao on 23-8-6.
//

#include <dlfcn.h>
#include <gtest/gtest.h>
#include <omnistack/module/module.hpp>

TEST(DataPlaneSampleModule, Load) {
    using namespace omnistack::data_plane;
    auto handle = dlopen("../lib/libomni_data_plane_sample_module.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    dlclose(handle);
}