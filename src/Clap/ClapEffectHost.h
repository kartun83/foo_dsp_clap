//
//  ClapEffectHost.h
//  foo_dsp_clap
//
//  A CLAP host specialised for hosting a single audio-effect plugin, used by both
//  the audio DSP (load / activate / processInterleaved / reset) and the config
//  popup (state, params, embedded GUI). Adapted from foo_tun_midi's ClapRenderer,
//  which encodes the load-bearing host-contract findings:
//    - supply a real, zeroed buffer for EVERY declared audio port (inputs
//      included) or JUCE-wrapped plugins crash;
//    - capture output port 0 ("Main") by convention.
//
//  Threading: state / params / gui calls are CLAP [main-thread] and the popup
//  uses them on the main thread. processInterleaved / reset run on foobar's
//  streaming thread — the same pragmatic single-instance-per-thread approach the
//  offline renderer proved against real plugins.
//

#pragma once

#include <clap/clap.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace foo_clap_dsp {

struct ClapParamInfo {
    clap_id     id = 0;
    std::string name;
    double      minValue = 0.0;
    double      maxValue = 1.0;
    double      defaultValue = 0.0;
    bool        isStepped = false;
};

class ClapEffectHost {
public:
    ClapEffectHost();
    ~ClapEffectHost();

    ClapEffectHost(const ClapEffectHost&) = delete;
    ClapEffectHost& operator=(const ClapEffectHost&) = delete;

    // Load the plugin `pluginId` (empty = first) from the .clap bundle. Does not
    // activate. Returns false on any failure (logged to the foobar console).
    bool load(const std::string& bundlePath, const std::string& pluginId);
    bool isLoaded() const;

    const std::string& displayName() const;

    // --- audio (streaming thread) -----------------------------------------
    // Activate at the given sample rate; blocks are processed in chunks of at
    // most maxFrames. Safe to call again to re-activate at a new rate.
    bool activate(double sampleRate, uint32_t maxFrames);
    void deactivate();
    void reset();                       // drop tails/filters (after a seek)
    bool isActive() const;
    double activeSampleRate() const;

    // Process `frames` of interleaved float audio in place. `channels` is the
    // host stream's channel count; mono and stereo are handled, wider streams
    // have their first two channels processed and the rest passed through.
    void processInterleaved(float* interleaved, uint32_t frames, uint32_t channels);

    double latencySeconds() const;

    // --- state (main-thread) ----------------------------------------------
    bool saveState(std::vector<uint8_t>& out);
    bool loadState(const uint8_t* data, size_t size);

    // --- params (main-thread) ---------------------------------------------
    uint32_t paramCount();
    bool paramInfoByIndex(uint32_t index, ClapParamInfo& out);
    double paramValue(clap_id id);
    bool paramValueToText(clap_id id, double value, std::string& out);
    // Queue a param change and, if active, apply it immediately via a short
    // silent process block (used by the popup's generic param UI).
    void setParamAndFlush(clap_id id, double value);

    // --- preset-load (main-thread) ----------------------------------------
    // Load a discovered preset via the plugin's preset-load extension.
    bool loadPreset(uint32_t locationKind, const std::string& location,
                    const std::string& loadKey);

    // --- gui (main-thread) ------------------------------------------------
    bool guiSupportsEmbeddedCocoa();
    // Create + embed the plugin GUI into parentNSView (an NSView*). Returns the
    // preferred size in *outW/*outH.
    bool guiCreateEmbedded(void* parentNSView, uint32_t* outW, uint32_t* outH);
    bool guiCanResize();
    void guiDestroy();
    // Invoked (on the main thread) when the plugin requests a host-side resize.
    void setResizeCallback(std::function<void(uint32_t, uint32_t)> cb);

    // Opaque to callers; public only so the file-local CLAP host callbacks in
    // ClapEffectHost.cpp can reference it.
    struct Impl;

private:
    std::unique_ptr<Impl> m_impl;
};

} // namespace foo_clap_dsp
