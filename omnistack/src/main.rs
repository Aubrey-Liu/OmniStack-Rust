mod modules;

use omnistack_core::engine::Engine;

fn main() {
    let config = r#"{
        "nodes": [
            { "id": "user", "name": "UserNode" },
            { "id": "eth_tx", "name": "EthSender" },
            { "id": "eth_rx", "name": "EthReceiver"},
            { "id": "io", "name": "IoNode" }
        ],
        "edges": [
            ["user", "eth_tx"],
            ["eth_tx", "io"],
            ["io", "eth_rx"],
            ["eth_rx", "user"]
        ]
    }"#;

    Engine::run(config).expect("failed to boot engine");

    // todo: start the omnistack server only once (lock file?)
}
