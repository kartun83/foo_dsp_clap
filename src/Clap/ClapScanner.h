//
//  ClapScanner.h
//  foo_dsp_clap
//
//  Enumerates installed CLAP *audio-effect* plugins from the standard macOS
//  locations so the config popup can offer a dropdown. Only each plugin's
//  descriptor is read (name / id / features) — the plugin is never instantiated
//  during a scan, so a heavyweight plugin can't boot or crash the host while
//  scanning.
//
//  The result is cached in memory and persisted (via ClapConfig) so the list is
//  instant on later launches; a rescan is on demand only.
//
//  Adapted from foo_tun_midi's ClapScanner (which filters for instruments).
//

#pragma once

#include <string>
#include <vector>

namespace foo_clap_dsp {

struct ClapPluginEntry {
    std::string name;   // descriptor display name
    std::string path;   // .clap bundle path
    std::string id;     // plugin id within the bundle (bundles can host several)
};

// Installed CLAP audio effects, sorted by name. `forceRescan` re-walks the
// filesystem and re-persists; otherwise a persisted/in-memory cache is returned
// (scanning the filesystem once if neither exists yet).
//
// NOTE: rescanning dlopens every .clap to read its features and macOS never
// unloads an Obj-C/JUCE image, so a rescan permanently raises this process's
// memory until foobar2000 restarts. Playback never scans (it reads the cache).
const std::vector<ClapPluginEntry>& clapEffects(bool forceRescan = false);

} // namespace foo_clap_dsp
