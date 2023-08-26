#include <omnistack/common/config.h>

namespace omnistack {
    
}

int main(int argc, char **argv) {
    /** 1. Load Configurations **/
    omnistack::config::ConfigManager::LoadFromDirectory("config");
    omnistack::config::ConfigManager::LoadFromDirectory("graph_config");

    /* init libraries */

    /* create engines */

    /* start control plane */

    return 0;
}