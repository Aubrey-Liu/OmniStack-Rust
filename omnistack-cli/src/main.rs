use std::path::{Path, PathBuf};

use clap::Parser;
use log::LevelFilter;

use omnistack_core::config::ConfigManager;
use omnistack_core::engine::Engine;

#[derive(Parser)]
struct Cli {
    #[arg(short, long)]
    stack: String,

    #[arg(short, long)]
    config: Option<PathBuf>,

    #[arg(short, long)]
    logging_level: Option<LevelFilter>,
}

fn main() {
    let cli = Cli::parse();

    env_logger::builder()
        .filter_level(cli.logging_level.unwrap_or(LevelFilter::Info))
        .init();

    let config_dir = cli.config.as_deref().unwrap_or(Path::new("config"));

    assert!(
        config_dir.is_dir(),
        "'{}' is not a directory",
        config_dir.display()
    );

    {
        ConfigManager::get_mut().load_dir(config_dir);
    }

    Engine::run(&cli.stack).unwrap();
}

#[global_allocator]
static GLOBAL: tikv_jemallocator::Jemalloc = tikv_jemallocator::Jemalloc;
