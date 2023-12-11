mod modules;

use omnistack_core::engine::Engine;

// todo: use tracing and tracing-subscriber

fn main() {
    let config = r#"{
        "nodes": [
            { "id": "user", "name": "UserNode" },
            { "id": "eth_tx", "name": "EthSender" },
            { "id": "eth_rx", "name": "EthReceiver" },
            { "id": "ipv4_tx", "name": "Ipv4Sender" },
            { "id": "ipv4_rx", "name": "Ipv4Receiver" },
            { "id": "udp_tx", "name": "UdpSender" },
            { "id": "udp_rx", "name": "UdpReceiver" },
            { "id": "io", "name": "IoNode" }
        ],
        "edges": [
            ["user", "udp_tx"],
            ["udp_tx", "ipv4_tx"],
            ["ipv4_tx", "eth_tx"],
            ["eth_tx", "io"],
            ["io", "eth_rx"],
            ["eth_rx", "ipv4_rx"],
            ["ipv4_rx", "udp_rx"],
            ["udp_rx", "user"]
        ]
    }"#;

    Engine::run(config).expect("failed to boot engine");

    // todo: start the omnistack server only once (lock file?)
}
