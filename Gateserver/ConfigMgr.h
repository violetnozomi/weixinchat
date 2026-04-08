//
// Created by EDPerson on 2026/2/5.
//

#ifndef WXCHAT_CONFIGMGR_H
#define WXCHAT_CONFIGMGR_H
#include <map>
#include "const.h"
struct SectionInfo {
    SectionInfo() {}
    ~SectionInfo() {
        _sections_data.clear();
    }
    SectionInfo(const SectionInfo &other) {
        _sections_data = other._sections_data;
    }
        SectionInfo& operator=(const SectionInfo& other) {
            if (this == &other ) {
                return *this;
            }
            _sections_data =  other._sections_data;
            return *this;
        }

    std::string operator[](const std::string& key) {
        if (_sections_data.find(key)==_sections_data.end()) {
            return "";
        }
        return _sections_data[key];
    }
    std::map<std::string,std::string> _sections_data;
};

class ConfigMgr {
public:

    ~ConfigMgr() {_config_map.clear();};
    SectionInfo operator[](const std::string& key) {
        if (_config_map.find(key)==_config_map.end()) {
            return SectionInfo();
        }
        return _config_map[key];
    }
    ConfigMgr& operator=(const ConfigMgr& other) {
        if (&other == this) {
            return *this;
        }
        _config_map = other._config_map;
        return *this;
    }
    ConfigMgr(const ConfigMgr& other) {
        _config_map = other._config_map;
    }
    static ConfigMgr& Inst();
    ConfigMgr();
private:
    std::map<std::string,SectionInfo> _config_map;
};


#endif //WXCHAT_CONFIGMGR_H