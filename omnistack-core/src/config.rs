use std::path::Path;
use std::sync::Mutex;

// TODO: use serde_json
use json::JsonValue;

use crate::protocol::*;

pub struct ConfigManager {
    graph_configs: Vec<GraphConfig>,
    stack_configs: Vec<StackConfig>,
}

impl ConfigManager {
    pub fn get() -> &'static Mutex<ConfigManager> {
        static CONFIG: Mutex<ConfigManager> = Mutex::new(ConfigManager {
            graph_configs: Vec::new(),
            stack_configs: Vec::new(),
        });

        &CONFIG
    }

    pub fn load_dir(&mut self, dir: &Path) {
        for entry in std::fs::read_dir(dir).unwrap() {
            let path = entry.unwrap().path();
            if path.is_dir() {
                self.load_dir(&path);
            } else {
                self.load_file(&path);
            }
        }
    }

    pub fn load_file(&mut self, path: &Path) {
        let config = std::fs::read_to_string(path).unwrap();
        let config = json::parse(config.as_str()).unwrap();

        let config_type = json_require_str(&config, "type");
        match config_type {
            "Graph" => {
                let graph_config = GraphConfig::from(&config);

                if self.get_graph_config(&graph_config.name).is_some() {
                    log::error!("duplicate graph config: {}, skipping ..", graph_config.name);
                    return;
                }

                log::debug!("loaded graph config: {}", graph_config.name);
                self.graph_configs.push(graph_config);
            }
            "Stack" => {
                let stack_config = StackConfig::from(&config);

                if self.get_stack_config(&stack_config.name).is_some() {
                    log::error!("duplicate stack config: {}, skipping ..", stack_config.name);
                    return;
                }

                log::debug!("loaded stack config: {}", stack_config.name);
                self.stack_configs.push(stack_config);
            }
            _ => log::error!("invalid config type in '{}'", path.display()),
        }
    }

    pub fn get_graph_config<'a>(&'a self, name: &str) -> Option<&'a GraphConfig> {
        self.graph_configs.iter().find(|g| g.name == name)
    }

    pub fn get_stack_config<'a>(&'a self, name: &str) -> Option<&'a StackConfig> {
        self.stack_configs.iter().find(|g| g.name == name)
    }
}

pub struct GraphConfig {
    pub name: String,
    pub modules: Vec<String>,
    pub links: Vec<(String, String)>,
}

impl From<&JsonValue> for GraphConfig {
    fn from(value: &JsonValue) -> Self {
        let name = json_require_str(value, "name").to_string();
        let modules: Vec<_> = json_require_value(value, "modules")
            .members()
            .map(|m| json_assert_str(m).to_string())
            .collect();
        let links: Vec<_> = json_require_value(value, "links")
            .members()
            .map(|l| {
                (
                    json_assert_str(&l[0]).to_string(),
                    json_assert_str(&l[1]).to_string(),
                )
            })
            .collect();

        Self {
            name,
            modules,
            links,
        }
    }
}

pub struct StackConfig {
    pub name: String,
    pub nics: Vec<NicConfig>,
    // pub arps: Vec<ArpEntry>,
    pub routes: Vec<Route>,
    pub graphs: Vec<GraphEntry>,
}

impl From<&JsonValue> for StackConfig {
    fn from(value: &JsonValue) -> Self {
        let name = json_require_str(value, "name").to_string();

        let nics = json_require_value(value, "nics");
        let nics: Vec<_> = nics.members().map(NicConfig::from).collect();

        // let arps = json_require_value(value, "arps");
        // let arps: Vec<_> = arps.members().map(ArpEntry::from).collect();

        let routes = json_require_value(value, "routes");
        let routes: Vec<_> = routes.members().map(Route::from).collect();

        let graphs = json_require_value(value, "graphs");
        let graphs: Vec<_> = graphs.members().map(GraphEntry::from).collect();

        Self {
            name,
            nics,
            // arps,
            routes,
            graphs,
        }
    }
}

pub struct GraphEntry {
    pub name: String,
    pub cpus: Vec<u32>,
}

impl From<&JsonValue> for GraphEntry {
    fn from(value: &JsonValue) -> Self {
        Self {
            name: json_require_str(value, "name").to_string(),
            cpus: json_require_value(value, "cpus")
                .members()
                .map(json_assert_number)
                .collect(),
        }
    }
}

#[derive(Debug)]
pub struct NicConfig {
    pub adapter_name: String,
    pub port: u16,
    pub ipv4: Ipv4Addr,
    pub netmask: Ipv4Addr,
}

impl From<&JsonValue> for NicConfig {
    fn from(value: &JsonValue) -> Self {
        Self {
            adapter_name: json_require_str(value, "adapter_name").to_string(),
            port: json_require_number(value, "port") as _,
            ipv4: json_require_str(value, "ipv4").parse::<Ipv4Addr>().unwrap(),
            netmask: json_require_str(value, "netmask")
                .parse::<Ipv4Addr>()
                .unwrap(),
        }
    }
}

// pub struct ArpEntry {
//     pub ipv4: Ipv4Addr,
//     pub mac: MacAddr,
// }
//
// impl From<&JsonValue> for ArpEntry {
//     fn from(value: &JsonValue) -> Self {
//         let mac: Vec<_> = json_require_str(value, "mac")
//             .split(':')
//             .map(|s| u8::from_str_radix(s, 16).unwrap())
//             .collect();
//         let mac = MacAddr::from_bytes(mac.try_into().unwrap());
//
//         Self {
//             ipv4: json_require_str(value, "ipv4").parse::<Ipv4Addr>().unwrap(),
//             mac,
//         }
//     }
// }

impl From<&JsonValue> for Route {
    fn from(value: &JsonValue) -> Self {
        Self::new(
            json_require_str(value, "ipv4").parse::<Ipv4Addr>().unwrap(),
            json_require_number(value, "cidr") as _,
            json_require_number(value, "nic") as _,
        )
    }
}

fn json_require_value<'a>(value: &'a JsonValue, key: &str) -> &'a JsonValue {
    if value[key].is_null() {
        panic!("required field `{}` is missing", key);
    }
    &value[key]
}

fn json_require_number(value: &JsonValue, key: &str) -> u32 {
    let value = json_require_value(value, key);
    if !value.is_number() {
        panic!("required field `{}` is not a number", key);
    }
    value.as_u32().unwrap()
}

fn json_assert_number(value: &JsonValue) -> u32 {
    if !value.is_number() {
        panic!("expect json value {:?} to be a number", value);
    }

    value.as_u32().unwrap()
}

fn json_assert_str(value: &JsonValue) -> &str {
    if !value.is_string() {
        panic!("expect json value {:?} to be a string", value);
    }

    value.as_str().unwrap()
}

fn json_require_str<'a>(value: &'a JsonValue, key: &str) -> &'a str {
    let value = json_require_value(value, key);
    if !value.is_string() {
        panic!("required field `{}` is not a string", key);
    }
    value.as_str().unwrap()
}
