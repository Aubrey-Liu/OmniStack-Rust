mod modules;

use std::path::Path;

use omnistack_core::config::ConfigManager;
use omnistack_core::engine::Engine;

fn main() {
    env_logger::init();

    // TODO: make it less likely to dead lock
    {
        let config_dir = Path::new("config");
        let mut config_manager = ConfigManager::get().lock().unwrap();
        config_manager.load_dir(config_dir);
    }

    Engine::run("UDP").unwrap();
}
