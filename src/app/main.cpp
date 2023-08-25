#include <omnistack/common/config.h>

int main(int argc, char **argv) {
    /** 1. Load Configurations **/
    omnistack::config::ConfigManager::LoadFromDirectory("config");
    omnistack::config::ConfigManager::LoadFromDirectory("graph_config");

    return 0;
}