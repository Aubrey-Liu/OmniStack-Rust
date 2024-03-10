mod modules;

use std::path::Path;

use omnistack_core::engine::{ConfigManager, Engine};

fn main() {
    env_logger::init();

    let config_dir = Path::new("omnistack/config");
    ConfigManager::load_file(&config_dir.join("udp.json"));
    ConfigManager::load_file(&config_dir.join("udp-multicore-stack.json"));
    Engine::run("UDP").expect("failed to boot engine");
}
