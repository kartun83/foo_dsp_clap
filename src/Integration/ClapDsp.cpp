//
//  ClapDsp.cpp
//  foo_dsp_clap
//
//  The foobar2000 DSP service: hosts a CLAP audio-effect plugin and runs the
//  playback stream through it. Modeled on the SDK's foo_sample/dsp_sample.cpp
//  (Mac config-popup wiring) but processing through ClapEffectHost.
//

#include "../fb2k_sdk.h"
#include "../Core/ClapDspConfig.h"
#include "../Core/ClapConfig.h"
#include "../Clap/ClapEffectHost.h"

#include <mutex>
#include <vector>

// Implemented in UI/ClapDspView.mm.
service_ptr ConfigureClapDsp(fb2k::hwnd_t parent, dsp_preset_edit_callback_v2::ptr callback);

namespace {

using namespace foo_clap_dsp;

class clap_dsp : public dsp_impl_base_t<dsp_v3> {
public:
    clap_dsp(const dsp_preset& in) {
        decodeSettings(in, m_settings);   // leaves m_settings empty on mismatch => passthrough
    }

    static GUID g_get_guid() { return kClapDspGuid; }
    static void g_get_name(pfc::string_base& out) { out = "CLAP Effect"; }

    // --- audio -------------------------------------------------------------
    bool on_chunk(audio_chunk* chunk, abort_callback&) override {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (!ensureReady(chunk->get_srate())) return true;   // passthrough

        const t_size frames = chunk->get_sample_count();
        const unsigned channels = chunk->get_channels();
        if (frames == 0 || channels == 0) return true;

        // audio_sample may be float or double; CLAP is float32. Convert via a
        // reusable scratch buffer either way.
        const t_size total = frames * channels;
        m_scratch.resize(total);
        const audio_sample* src = chunk->get_data();
        for (t_size i = 0; i < total; ++i) m_scratch[i] = (float)src[i];

        m_host.processInterleaved(m_scratch.data(), (uint32_t)frames, channels);

        audio_sample* dst = chunk->get_data();
        for (t_size i = 0; i < total; ++i) dst[i] = (audio_sample)m_scratch[i];
        return true;
    }

    void on_endofplayback(abort_callback&) override {}
    void on_endoftrack(abort_callback&) override {}

    void flush() override {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_host.isActive()) m_host.reset();
    }

    double get_latency() override {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_host.isLoaded() ? m_host.latencySeconds() : 0.0;
    }

    bool need_track_change_mark() override { return false; }

    // --- live reconfigure (dsp_v3) ----------------------------------------
    // Same plugin -> apply the new state in place (no audio drop). Different
    // plugin -> return false so the DSP manager recreates us cleanly.
    bool apply_preset(const dsp_preset& p) override {
        ClapDspSettings next;
        if (!decodeSettings(p, next)) return false;
        std::lock_guard<std::mutex> lk(m_mutex);
        bool samePlugin = next.pluginPath == m_settings.pluginPath &&
                          next.pluginId == m_settings.pluginId;
        if (!samePlugin) return false;
        m_settings = next;
        if (m_host.isLoaded() && !m_settings.state.empty())
            m_host.loadState(m_settings.state.data(), m_settings.state.size());
        return true;
    }

    // --- config / presets --------------------------------------------------
    static bool g_get_default_preset(dsp_preset& out) {
        ClapDspSettings empty;   // no plugin selected => passthrough
        encodeSettings(empty, out);
        return true;
    }
    static bool g_have_config_popup() { return true; }
#ifdef __APPLE__
    static service_ptr g_show_config_popup(fb2k::hwnd_t parent,
                                           dsp_preset_edit_callback_v2::ptr callback) {
        return ConfigureClapDsp(parent, callback);
    }
#endif

private:
    // Load + activate the host to match the incoming stream. Returns false when
    // there is no usable plugin (caller passes audio through untouched).
    bool ensureReady(unsigned srate) {
        if (!m_settings.hasPlugin()) return false;
        if (m_failed) return false;

        if (!m_host.isLoaded()) {
            if (!m_host.load(m_settings.pluginPath, m_settings.pluginId)) {
                m_failed = true;   // don't retry every chunk
                return false;
            }
            if (!m_settings.state.empty())
                m_host.loadState(m_settings.state.data(), m_settings.state.size());
        }
        if (!m_host.isActive() || (unsigned)m_host.activeSampleRate() != srate) {
            if (!m_host.activate((double)srate, kMaxBlock)) { m_failed = true; return false; }
        }
        return true;
    }

    static constexpr uint32_t kMaxBlock = 4096;

    std::mutex m_mutex;              // guards host between audio + control threads
    ClapEffectHost m_host;
    ClapDspSettings m_settings;
    std::vector<float> m_scratch;
    bool m_failed = false;
};

static dsp_factory_t<clap_dsp> g_clap_dsp_factory;

} // namespace
