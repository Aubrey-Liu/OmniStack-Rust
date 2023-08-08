//
// Created by liuhao on 23-8-6.
//

#include <dlfcn.h>
#include <gtest/gtest.h>
#include <omnistack/module/module.hpp>

TEST(DataPlaneSampleModule, Load) {
    using namespace omnistack::data_plane;
    auto handle = dlopen("../../../src/data_plane/libomni_data_plane_sample_module.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    dlclose(handle);
}

TEST(DataPlaneSampleModule, Create) {
    using namespace omnistack::data_plane;
    auto handle = dlopen("../../../src/data_plane/libomni_data_plane_sample_module.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto sample_module = ModuleFactory::instance().Create("SampleModule");
    ASSERT_NE(sample_module, nullptr);
    dlclose(handle);
}

TEST(DataPlaneSampleModule, Functions) {
    using namespace omnistack::data_plane;
    auto handle = dlopen("../../../src/data_plane/libomni_data_plane_sample_module.so", RTLD_NOW | RTLD_GLOBAL);
    ASSERT_NE(handle, nullptr);
    auto sample_module = ModuleFactory::instance().Create("SampleModule");
    ASSERT_NE(sample_module, nullptr);
    auto result = sample_module->DefaultFilter(nullptr);
    ASSERT_EQ(result, true);
    result = sample_module->GetFilter("upstream_module", 0)(nullptr);
    ASSERT_EQ(result, true);
    auto packet = sample_module->MainLogic(nullptr);
    ASSERT_EQ(packet, nullptr);
    packet = sample_module->TimerLogic(0);
    ASSERT_EQ(packet, nullptr);
    result = sample_module->allow_duplication_();
    ASSERT_EQ(result, true);
    auto type = sample_module->type_();
    ASSERT_EQ(type, BaseModule::ModuleType::kReadOnly);
    dlclose(handle);
}