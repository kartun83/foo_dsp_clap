//
//  ClapDspConfig.cpp
//  foo_dsp_clap
//

#include "ClapDspConfig.h"

#include <cstring>

namespace foo_clap_dsp {

namespace {

constexpr uint8_t kBlobVersion = 1;

void putU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back((uint8_t)(v & 0xFF));
    b.push_back((uint8_t)((v >> 8) & 0xFF));
    b.push_back((uint8_t)((v >> 16) & 0xFF));
    b.push_back((uint8_t)((v >> 24) & 0xFF));
}

void putBytes(std::vector<uint8_t>& b, const void* data, uint32_t len) {
    putU32(b, len);
    const uint8_t* p = (const uint8_t*)data;
    b.insert(b.end(), p, p + len);
}

bool getU32(const uint8_t* d, size_t size, size_t& pos, uint32_t& out) {
    if (pos + 4 > size) return false;
    out = (uint32_t)d[pos] | ((uint32_t)d[pos + 1] << 8) |
          ((uint32_t)d[pos + 2] << 16) | ((uint32_t)d[pos + 3] << 24);
    pos += 4;
    return true;
}

bool getBytes(const uint8_t* d, size_t size, size_t& pos, std::string& out) {
    uint32_t len = 0;
    if (!getU32(d, size, pos, len)) return false;
    if (pos + len > size) return false;
    out.assign((const char*)d + pos, len);
    pos += len;
    return true;
}

} // namespace

void encodeSettings(const ClapDspSettings& in, dsp_preset& out) {
    std::vector<uint8_t> b;
    b.push_back(kBlobVersion);
    putBytes(b, in.pluginPath.data(), (uint32_t)in.pluginPath.size());
    putBytes(b, in.pluginId.data(), (uint32_t)in.pluginId.size());
    putBytes(b, in.presetName.data(), (uint32_t)in.presetName.size());
    putBytes(b, in.state.data(), (uint32_t)in.state.size());
    out.set_owner(kClapDspGuid);
    out.set_data(b.data(), b.size());
}

bool decodeSettings(const dsp_preset& in, ClapDspSettings& out) {
    if (in.get_owner() != kClapDspGuid) return false;
    const uint8_t* d = (const uint8_t*)in.get_data();
    size_t size = in.get_data_size();
    if (!d || size < 1) return false;

    size_t pos = 0;
    uint8_t version = d[pos++];
    if (version != kBlobVersion) return false;

    std::string stateStr;
    if (!getBytes(d, size, pos, out.pluginPath)) return false;
    if (!getBytes(d, size, pos, out.pluginId)) return false;
    if (!getBytes(d, size, pos, out.presetName)) return false;
    if (!getBytes(d, size, pos, stateStr)) return false;
    out.state.assign(stateStr.begin(), stateStr.end());
    return true;
}

} // namespace foo_clap_dsp
