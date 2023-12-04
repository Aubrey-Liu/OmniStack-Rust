mod modules;

use omnistack_core::engine::Engine;

fn main() {
    let config = r#"{
        "nodes": [
            { "id": "user", "name": "UserNode" },
            { "id": "io", "name": "IoNode" }
        ],
        "edges": [
            ["user", "io"]
        ]
    }"#;

    Engine::run(config).expect("failed to boot engine");

    // todo: start the omnistack server only once (lock file?)
}
