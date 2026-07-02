//
//  ClapConfig.h
//  foo_dsp_clap
//
//  Persistent configuration via fb2k::configStore. On macOS v2, cfg_var does NOT
//  persist reliably — configStore is the supported API. Used for the cached
//  plugin scan and the "last selected" defaults offered in the config popup.
//  (The per-instance plugin/preset/state selection travels in the DSP preset
//  blob instead — see ClapDspConfig.h.)
//

#pragma once

#include "../fb2k_sdk.h"
#include <string>

namespace clap_dsp_config {

static const char* const kConfigPrefix = "foo_dsp_clap.";

// Config keys.
static const char* const kKeyClapPluginList = "clap_plugin_list";  // cached effect scan
static const char* const kKeyLastPluginPath = "last_plugin_path";  // popup default
static const char* const kKeyLastPluginId   = "last_plugin_id";

inline std::string getConfigString(const char* key, const char* defaultVal) {
    try {
        auto store = fb2k::configStore::get();
        pfc::string8 fullKey;
        fullKey << kConfigPrefix << key;
        fb2k::stringRef val = store->getConfigString(fullKey.c_str(), defaultVal);
        return val->c_str();
    } catch (...) {
        return defaultVal ? defaultVal : "";
    }
}

inline void setConfigString(const char* key, const char* value) {
    try {
        auto store = fb2k::configStore::get();
        pfc::string8 fullKey;
        fullKey << kConfigPrefix << key;
        store->setConfigString(fullKey.c_str(), value);
    } catch (...) {
        // best effort
    }
}

} // namespace clap_dsp_config
