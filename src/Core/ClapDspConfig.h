//
//  ClapDspConfig.h
//  foo_dsp_clap
//
//  Serialization of a DSP instance's settings into the foobar2000 dsp_preset
//  blob. The blob travels with saved DSP presets and is restored next session.
//  It carries the selected plugin plus the plugin's FULL CLAP state (clap.state),
//  so all parameters and internal modes are captured faithfully.
//
//  Wire format (little-endian), version 1:
//    u8   version (=1)
//    u32  pluginPathLen, bytes...
//    u32  pluginIdLen,   bytes...
//    u32  presetNameLen, bytes...   (display label only)
//    u32  stateLen,      bytes...   (opaque clap.state blob)
//

#pragma once

#include "../fb2k_sdk.h"   // dsp_preset, GUID

#include <cstdint>
#include <string>
#include <vector>

namespace foo_clap_dsp {

// Owner GUID for our dsp_preset blobs / dsp registration.
// {6E2F1A94-3C7D-4B0E-9E21-7F5A2C8D1B44}
static const GUID kClapDspGuid =
    { 0x6e2f1a94, 0x3c7d, 0x4b0e, { 0x9e, 0x21, 0x7f, 0x5a, 0x2c, 0x8d, 0x1b, 0x44 } };

struct ClapDspSettings {
    std::string pluginPath;          // .clap bundle (empty = passthrough)
    std::string pluginId;            // plugin within the bundle (empty = first)
    std::string presetName;          // last-loaded preset label (display only)
    std::vector<uint8_t> state;      // opaque clap.state blob (may be empty)

    bool hasPlugin() const { return !pluginPath.empty(); }
};

// Encode settings into a dsp_preset (sets owner + data).
void encodeSettings(const ClapDspSettings& in, dsp_preset& out);

// Decode a dsp_preset back into settings. Returns false if the blob is not ours
// or is malformed (caller should treat as "no plugin / passthrough").
bool decodeSettings(const dsp_preset& in, ClapDspSettings& out);

} // namespace foo_clap_dsp
