//
// Created by EDPerson on 2026/2/5.
//

#include "ConfigMgr.h"
ConfigMgr::ConfigMgr() {
    boost::filesystem::path current_path = boost::filesystem::current_path();
    boost::filesystem::path config_path = current_path/"config.ini";
    std::cout << "Config path: " << config_path << std::endl;
    boost::property_tree::ptree pt;
    boost::property_tree::read_ini(config_path.string(), pt);
    for (auto& section:pt) {
        const std::string strinsection_name = section.first;
        const boost::property_tree::ptree pt_section = section.second;
        std::map<std::string, std::string> section_map;
        for (auto& key_value_pair:pt_section) {
            const std::string key = key_value_pair.first;
            const std::string value = key_value_pair.second.get_value<std::string>();
            section_map[key] = value;
        }
        SectionInfo section_info;
        section_info._sections_data = section_map;
        _config_map[strinsection_name] = section_info;
        for (const auto& section_entry : _config_map) {
            const std::string& section_name = section_entry.first;
            SectionInfo section_config = section_entry.second;
            std::cout << "[" << section_name << "]" << std::endl;
            for (const auto& key_value_pair : section_config._sections_data) {
                std::cout << key_value_pair.first << "=" << key_value_pair.second << std::endl;
            }
        }
    }
}
 ConfigMgr&  ConfigMgr::Inst() {
    static ConfigMgr cfg_mgr;
    return cfg_mgr;
}