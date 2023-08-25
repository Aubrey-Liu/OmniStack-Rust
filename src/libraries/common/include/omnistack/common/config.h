#ifndef __OMNISTACK_COMMON_CONFIG_HPP
#define __OMNISTACK_COMMON_CONFIG_HPP

#include <jsoncpp/json/json.h>
#include <vector>
#include <map>
#include <iostream>
#include <memory>
#include <arpa/inet.h>

#define REQUIRED_STR(node, name, arg) \
    do { \
        if (node[name].isNull()) { \
            std::cerr << "Required field " << name << " is missing" << std::endl; \
            exit(1); \
        } \
        if (!node[name].isString()) { \
            std::cerr << "Required field " << name << " is not a string" << std::endl; \
            exit(1); \
        } \
        arg = node[name].asString(); \
    } while (0)

#define REQUIRED_INT(node, name, arg) \
    do { \
        if (node[name].isNull()) { \
            std::cerr << "Required field " << name << " is missing" << std::endl; \
            exit(1); \
        } \
        if (!node[name].isInt()) { \
            std::cerr << "Required field " << name << " is not an integer" << std::endl; \
            exit(1); \
        } \
        arg = node[name].asInt(); \
    } while (0)

#define ASSIGN_STR(node, name, arg, default_value) \
    do { \
        if (node[name].isNull()) { \
            arg = default_value; \
        } else { \
            if (!node[name].isString()) { \
                std::cerr << "Required field " << name << " is not a string" << std::endl; \
                exit(1); \
            } \
            arg = node[name].asString(); \
        } \
    } while (0)

#define ASSIGN_INT(node, name, arg, default_value) \
    do { \
        if (node[name].isNull()) { \
            arg = default_value; \
        } else { \
            if (!node[name].isInt()) { \
                std::cerr << "Required field " << name << " is not an integer" << std::endl; \
                exit(1); \
            } \
            arg = node[name].asInt(); \
        } \
    } while (0)

namespace omnistack {
    namespace config {
        struct StackConfig {
            StackConfig(Json::Value& root) {
                REQUIRED_STR(root, "name", name_);
                if (!root["nics"].isNull()) {
                    for (auto nic : root["nics"])
                        nics_.emplace_back(NicConfig(nic));
                }
                if (!root["arps"].isNull()) {
                    for (auto arp : root["arps"])
                        arps_.emplace_back(ArpEntry(arp));
                }
                if (!root["routes"].isNull()) {
                    for (auto route : root["routes"])
                        routes_.emplace_back(RouteEntry(route));
                }
                if (!root["graphs"].isNull()) {
                    for (auto graph : root["graphs"])
                        graphs_.emplace_back(GraphEntry(graph));
                }
            }

            struct NicConfig {
                std::string driver_name_;
                int port_;
                std::string ipv4_;
                uint32_t ipv4_raw_;
                std::string netmask_;
                uint32_t netmask_raw_;

                NicConfig(Json::Value root) {
                    REQUIRED_STR(root, "driver_name", driver_name_);
                    REQUIRED_INT(root, "port", port_);
                    REQUIRED_STR(root, "ipv4", ipv4_);
                    REQUIRED_STR(root, "netmask", netmask_);
                    ipv4_raw_ = inet_addr(ipv4_.c_str());
                    netmask_raw_ = inet_addr(netmask_.c_str());
                }
            };
            struct ArpEntry {
                std::string ipv4_;
                uint32_t ipv4_raw_;
                std::string mac_;
                char mac_raw_[6];

                ArpEntry(Json::Value root) {
                    REQUIRED_STR(root, "ipv4", ipv4_);
                    REQUIRED_STR(root, "mac", mac_);
                    ipv4_raw_ = inet_addr(ipv4_.c_str());
                    sscanf(mac_.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &mac_raw_[0], &mac_raw_[1], &mac_raw_[2], &mac_raw_[3], &mac_raw_[4], &mac_raw_[5]);
                }
            };
            struct GraphEntry {
                std::vector<int> cpus_;
                std::string name_;
                std::string structure_;

                GraphEntry(Json::Value root) {
                    REQUIRED_STR(root, "name", name_);
                    REQUIRED_STR(root, "structure", structure_);
                    if (!root["cpus"].isNull()) {
                        for (auto cpu : root["cpus"])
                            cpus_.emplace_back(cpu.asInt());
                    }
                }
            };
            struct RouteEntry {
                std::string ipv4_;
                uint32_t ipv4_raw_;
                uint32_t cidr_;
                int nic_;

                RouteEntry(Json::Value root) {
                    REQUIRED_STR(root, "ipv4", ipv4_);
                    REQUIRED_INT(root, "cidr", cidr_);
                    REQUIRED_INT(root, "nic", nic_);
                    ipv4_raw_ = inet_addr(ipv4_.c_str());
                }
            };

            inline const std::vector<GraphEntry>& GetGraphEntries() const {return graphs_;}
            inline const std::vector<NicConfig>& GetNicConfigs() const {return nics_;}
            inline const std::vector<ArpEntry>& GetArpEntries() const {return arps_;}
            inline const std::vector<RouteEntry>& GetRouteEntries() const {return routes_;}
            
        private:
            std::string name_;
            std::vector<NicConfig> nics_;
            std::vector<ArpEntry> arps_;
            std::vector<RouteEntry> routes_;
            std::vector<GraphEntry> graphs_;
        };

        struct GraphConfig {
            GraphConfig(Json::Value& root) {
                REQUIRED_STR(root, "name", name_);
                if (!root["modules"].isNull()) {
                    for (auto module : root["modules"])
                        modules_.emplace_back(module.asString());
                }
                if (!root["links"].isNull()) {
                    for (auto link : root["links"]) {
                        std::string src, dst;
                        REQUIRED_STR(link, "src", src);
                        REQUIRED_STR(link, "dst", dst);
                        links_.emplace_back(std::make_pair(src, dst));
                    }
                }
                if (!root["groups"].isNull()) {
                    for (auto group : root["groups"]) {
                        std::vector<int> group_vec;
                        for (auto eid : group) {
                            if (!eid.isInt()) {
                                std::cerr << ("Group element is not an integer");
                                exit(1);
                            }
                            if (eid.asInt() >= links_.size()) {
                                std::cerr << ("Group out of range");
                                exit(1);
                            }
                            group_vec.emplace_back(eid.asInt());
                        }
                        groups_.emplace_back(group_vec);
                    }
                }
            }

            inline const std::vector<std::string>& GetModules() const {return modules_;}
            inline const std::vector<std::pair<std::string, std::string>>& GetLinks() const {return links_;}
            inline const std::vector<std::vector<int>>& GetGroups() const {return groups_;}
            inline const std::string& GetName() const {return name_;}

        private:
            std::string name_;
            std::vector<std::string> modules_;
            std::vector<std::pair<std::string, std::string>> links_;
            std::vector<std::vector<int>> groups_;
        };

        class ConfigManager {
        public:
            static void LoadFromDirectory(const std::string& path);
            static void LoadFromFile(const std::string& file);

            static const StackConfig& GetStackConfig(const std::string& name);
            static const GraphConfig& GetGraphConfig(const std::string& name);
        private:
            static std::map<std::string, StackConfig*> stack_configs_;
            static std::map<std::string, GraphConfig*> graph_configs_;
        };
    }
}

#undef REQUIRED_STR
#undef REQUIRED_INT
#undef ASSIGN_STR
#undef ASSIGN_INT

#endif