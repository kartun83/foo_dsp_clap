//
//  ClapEffectHost.cpp
//  foo_dsp_clap
//
//  See ClapEffectHost.h. Ports the proven CLAP host from foo_tun_midi's
//  ClapRenderer into a real-time effect host (audio in -> audio out) and adds the
//  state / params / GUI extensions the DSP config popup needs.
//

#include "ClapEffectHost.h"
#include "../fb2k_sdk.h"   // console::*

#include <clap/ext/state.h>
#include <clap/ext/params.h>
#include <clap/ext/gui.h>
#include <clap/ext/latency.h>
#include <clap/ext/preset-load.h>
#include <dlfcn.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>

namespace foo_clap_dsp {

namespace {

std::string bundleBinary(const std::string& bundlePath) {
    std::string name = bundlePath;
    while (!name.empty() && name.back() == '/') name.pop_back();
    auto slash = name.find_last_of('/');
    std::string leaf = (slash == std::string::npos) ? name : name.substr(slash + 1);
    if (leaf.size() > 5 && leaf.substr(leaf.size() - 5) == ".clap")
        leaf = leaf.substr(0, leaf.size() - 5);
    return name + "/Contents/MacOS/" + leaf;
}

// --- input-event list backed by a vector of param-value events -------------
struct EventStore {
    std::vector<clap_event_param_value_t> params;
    std::vector<const clap_event_header_t*> order;
};
uint32_t evStoreSize(const clap_input_events_t* l) {
    return (uint32_t)((EventStore*)l->ctx)->order.size();
}
const clap_event_header_t* evStoreGet(const clap_input_events_t* l, uint32_t i) {
    return ((EventStore*)l->ctx)->order[i];
}
bool outTryPush(const clap_output_events_t*, const clap_event_header_t*) { return true; }

// --- state streams ---------------------------------------------------------
struct OStreamCtx { std::vector<uint8_t>* buf; };
int64_t ostreamWrite(const clap_ostream_t* s, const void* buffer, uint64_t size) {
    auto* buf = ((OStreamCtx*)s->ctx)->buf;
    const uint8_t* p = (const uint8_t*)buffer;
    buf->insert(buf->end(), p, p + size);
    return (int64_t)size;
}
struct IStreamCtx { const uint8_t* data; size_t size; size_t pos; };
int64_t istreamRead(const clap_istream_t* s, void* buffer, uint64_t size) {
    auto* c = (IStreamCtx*)s->ctx;
    size_t todo = std::min<size_t>((size_t)size, c->size - c->pos);
    if (todo) memcpy(buffer, c->data + c->pos, todo);
    c->pos += todo;
    return (int64_t)todo;
}

} // namespace

struct ClapEffectHost::Impl {
    void* dl = nullptr;
    const clap_plugin_entry_t* entry = nullptr;
    const clap_plugin_t* plugin = nullptr;
    clap_host_t host{};
    clap_host_gui_t hostGui{};
    std::atomic<bool> callbackRequested{false};
    std::function<void(uint32_t, uint32_t)> resizeCb;

    std::string name;

    // audio state
    static constexpr uint32_t kMaxBlock = 4096;
    bool active = false;
    bool processing = false;
    double sampleRate = 0.0;

    // bus layout + buffers (a channel buffer per declared channel; port views)
    std::vector<uint32_t> inPortCh, outPortCh;
    std::vector<std::vector<float>> inStore, outStore;
    std::vector<std::vector<float*>> inPtrs, outPtrs;
    std::vector<clap_audio_buffer_t> inBufs, outBufs;

    // queued param changes (main thread -> audio thread)
    std::mutex paramMutex;
    std::vector<clap_event_param_value_t> pendingParams;

    // gui state
    bool guiCreated = false;

    long long steady = 0;

    ~Impl() { teardown(); }

    void teardown() {
        if (plugin) {
            if (guiCreated) {
                auto* gui = (const clap_plugin_gui_t*)plugin->get_extension(plugin, CLAP_EXT_GUI);
                if (gui) gui->destroy(plugin);
                guiCreated = false;
            }
            if (processing) { plugin->stop_processing(plugin); processing = false; }
            if (active) { plugin->deactivate(plugin); active = false; }
            plugin->destroy(plugin);
            plugin = nullptr;
        }
        if (entry) { entry->deinit(); entry = nullptr; }
        if (dl) { dlclose(dl); dl = nullptr; }
    }

    void buildBuses(const std::vector<uint32_t>& portCh,
                    std::vector<std::vector<float>>& store,
                    std::vector<std::vector<float*>>& ptrs,
                    std::vector<clap_audio_buffer_t>& bufs) {
        bufs.assign(portCh.size(), clap_audio_buffer_t{});
        ptrs.assign(portCh.size(), {});
        size_t total = 0; for (uint32_t c : portCh) total += c;
        store.assign(total, std::vector<float>(kMaxBlock, 0.f));
        size_t idx = 0;
        for (size_t p = 0; p < portCh.size(); ++p) {
            ptrs[p].resize(portCh[p]);
            for (uint32_t c = 0; c < portCh[p]; ++c) ptrs[p][c] = store[idx++].data();
            bufs[p].data32 = ptrs[p].data();
            bufs[p].channel_count = portCh[p];
        }
    }

    // Run one <= kMaxBlock block. `inter` points at `frames*channels` samples.
    void processBlock(float* inter, uint32_t frames, uint32_t channels) {
        // Zero every declared channel buffer up to `frames`.
        for (auto& ch : inStore)  std::fill(ch.begin(), ch.begin() + frames, 0.f);
        for (auto& ch : outStore) std::fill(ch.begin(), ch.begin() + frames, 0.f);

        // Deinterleave up to 2 channels into input port 0.
        uint32_t inCh = inPortCh.empty() ? 0 : inPortCh[0];
        uint32_t procCh = std::min<uint32_t>(channels, 2);
        for (uint32_t c = 0; c < procCh && c < inCh; ++c) {
            float* dst = inPtrs[0][c];
            for (uint32_t i = 0; i < frames; ++i) dst[i] = inter[i * channels + c];
        }
        // Mono host into a stereo-input plugin: feed the mono signal to both.
        if (channels == 1 && inCh >= 2) {
            float* src = inPtrs[0][0];
            std::copy(src, src + frames, inPtrs[0][1]);
        }

        EventStore store;
        {
            std::lock_guard<std::mutex> lk(paramMutex);
            store.params = std::move(pendingParams);
            pendingParams.clear();
        }
        for (auto& e : store.params) { e.header.time = 0; store.order.push_back(&e.header); }

        clap_input_events_t inEv{};   inEv.ctx = &store; inEv.size = evStoreSize; inEv.get = evStoreGet;
        clap_output_events_t outEv{}; outEv.ctx = nullptr; outEv.try_push = outTryPush;

        clap_process_t proc{};
        proc.steady_time = steady;
        proc.frames_count = frames;
        proc.transport = nullptr;
        proc.audio_inputs = inBufs.empty() ? nullptr : inBufs.data();
        proc.audio_inputs_count = (uint32_t)inBufs.size();
        proc.audio_outputs = outBufs.empty() ? nullptr : outBufs.data();
        proc.audio_outputs_count = (uint32_t)outBufs.size();
        proc.in_events = &inEv;
        proc.out_events = &outEv;

        clap_process_status st = plugin->process(plugin, &proc);
        steady += frames;
        if (st == CLAP_PROCESS_ERROR) return;   // leave input untouched (passthrough)

        // Interleave output port 0 back into the host buffer.
        uint32_t outCh = outPortCh.empty() ? 0 : outPortCh[0];
        for (uint32_t c = 0; c < procCh; ++c) {
            const float* src = outPtrs[0][c < outCh ? c : 0];
            for (uint32_t i = 0; i < frames; ++i) inter[i * channels + c] = src[i];
        }
    }
};

// --- host callbacks --------------------------------------------------------
namespace {
const void* hostGetExtension(const clap_host_t* h, const char* id) {
    auto* d = (ClapEffectHost::Impl*)h->host_data;
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &d->hostGui;
    return nullptr;
}
void hostRequestRestart(const clap_host_t*) {}
void hostRequestProcess(const clap_host_t*) {}
void hostRequestCallback(const clap_host_t* h) {
    ((ClapEffectHost::Impl*)h->host_data)->callbackRequested = true;
}
void guiResizeHintsChanged(const clap_host_t*) {}
bool guiRequestResize(const clap_host_t* h, uint32_t w, uint32_t hgt) {
    auto* d = (ClapEffectHost::Impl*)h->host_data;
    if (d->resizeCb) { d->resizeCb(w, hgt); return true; }
    return false;
}
bool guiRequestShow(const clap_host_t*) { return true; }
bool guiRequestHide(const clap_host_t*) { return true; }
void guiClosed(const clap_host_t*, bool) {}
} // namespace

ClapEffectHost::ClapEffectHost() : m_impl(std::make_unique<Impl>()) {}
ClapEffectHost::~ClapEffectHost() = default;

bool ClapEffectHost::isLoaded() const { return m_impl->plugin != nullptr; }
bool ClapEffectHost::isActive() const { return m_impl->active; }
double ClapEffectHost::activeSampleRate() const { return m_impl->sampleRate; }
const std::string& ClapEffectHost::displayName() const { return m_impl->name; }

bool ClapEffectHost::load(const std::string& bundlePath, const std::string& pluginId) {
    Impl& d = *m_impl;
    if (d.plugin) d.teardown();

    d.dl = dlopen(bundleBinary(bundlePath).c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!d.dl) { console::error("foo_dsp_clap: CLAP dlopen failed"); return false; }

    d.entry = (const clap_plugin_entry_t*)dlsym(d.dl, "clap_entry");
    if (!d.entry) { console::error("foo_dsp_clap: no clap_entry in bundle"); d.teardown(); return false; }
    if (!d.entry->init(bundlePath.c_str())) {
        console::error("foo_dsp_clap: clap entry init failed"); d.teardown(); return false;
    }

    auto* factory = (const clap_plugin_factory_t*)d.entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    if (!factory || factory->get_plugin_count(factory) == 0) {
        console::error("foo_dsp_clap: CLAP bundle has no plugins"); d.teardown(); return false;
    }
    const clap_plugin_descriptor_t* desc = factory->get_plugin_descriptor(factory, 0);
    if (!pluginId.empty()) {
        uint32_t n = factory->get_plugin_count(factory);
        for (uint32_t i = 0; i < n; ++i) {
            const clap_plugin_descriptor_t* cand = factory->get_plugin_descriptor(factory, i);
            if (cand && cand->id && pluginId == cand->id) { desc = cand; break; }
        }
    }
    if (!desc) { d.teardown(); return false; }
    d.name = (desc->name && desc->name[0]) ? desc->name : "CLAP effect";

    d.host.clap_version = CLAP_VERSION;
    d.host.host_data = &d;
    d.host.name = "foo_dsp_clap";
    d.host.vendor = "kartun83";
    d.host.url = "https://github.com/kartun83/foo_dsp_clap";
    d.host.version = "0.1.0";
    d.host.get_extension = hostGetExtension;
    d.host.request_restart = hostRequestRestart;
    d.host.request_process = hostRequestProcess;
    d.host.request_callback = hostRequestCallback;

    d.hostGui.resize_hints_changed = guiResizeHintsChanged;
    d.hostGui.request_resize = guiRequestResize;
    d.hostGui.request_show = guiRequestShow;
    d.hostGui.request_hide = guiRequestHide;
    d.hostGui.closed = guiClosed;

    d.plugin = factory->create_plugin(factory, &d.host, desc->id);
    if (!d.plugin || !d.plugin->init(d.plugin)) {
        console::error("foo_dsp_clap: CLAP create/init failed"); d.teardown(); return false;
    }

    // Mirror the plugin's exact bus layout (inputs included).
    d.inPortCh.clear(); d.outPortCh.clear();
    auto* ap = (const clap_plugin_audio_ports_t*)d.plugin->get_extension(d.plugin, CLAP_EXT_AUDIO_PORTS);
    if (ap) {
        uint32_t nin = ap->count(d.plugin, true), nout = ap->count(d.plugin, false);
        for (uint32_t i = 0; i < nin; ++i) {
            clap_audio_port_info_t info{};
            d.inPortCh.push_back(ap->get(d.plugin, i, true, &info) ? info.channel_count : 2);
        }
        for (uint32_t i = 0; i < nout; ++i) {
            clap_audio_port_info_t info{};
            d.outPortCh.push_back(ap->get(d.plugin, i, false, &info) ? info.channel_count : 2);
        }
    }
    if (d.inPortCh.empty()) d.inPortCh.push_back(2);   // ensure a stereo input path
    if (d.outPortCh.empty()) d.outPortCh.push_back(2);
    return true;
}

bool ClapEffectHost::activate(double sampleRate, uint32_t maxFrames) {
    Impl& d = *m_impl;
    if (!d.plugin) return false;
    if (d.active) deactivate();
    d.sampleRate = sampleRate > 0 ? sampleRate : 48000.0;
    uint32_t maxB = std::min(maxFrames ? maxFrames : Impl::kMaxBlock, Impl::kMaxBlock);

    d.buildBuses(d.inPortCh, d.inStore, d.inPtrs, d.inBufs);
    d.buildBuses(d.outPortCh, d.outStore, d.outPtrs, d.outBufs);

    if (!d.plugin->activate(d.plugin, d.sampleRate, 1, maxB)) {
        console::error("foo_dsp_clap: CLAP activate failed"); return false;
    }
    d.active = true;
    if (!d.plugin->start_processing(d.plugin)) {
        console::error("foo_dsp_clap: CLAP start_processing failed");
        d.plugin->deactivate(d.plugin); d.active = false; return false;
    }
    d.processing = true;
    d.steady = 0;
    return true;
}

void ClapEffectHost::deactivate() {
    Impl& d = *m_impl;
    if (!d.plugin) return;
    if (d.processing) { d.plugin->stop_processing(d.plugin); d.processing = false; }
    if (d.active) { d.plugin->deactivate(d.plugin); d.active = false; }
}

void ClapEffectHost::reset() {
    Impl& d = *m_impl;
    if (d.plugin && d.active) d.plugin->reset(d.plugin);
    d.steady = 0;
}

void ClapEffectHost::processInterleaved(float* inter, uint32_t frames, uint32_t channels) {
    Impl& d = *m_impl;
    if (!d.plugin || !d.active || !d.processing || frames == 0 || channels == 0) return;
    uint32_t off = 0;
    while (off < frames) {
        uint32_t n = std::min<uint32_t>(frames - off, Impl::kMaxBlock);
        d.processBlock(inter + (size_t)off * channels, n, channels);
        off += n;
    }
}

double ClapEffectHost::latencySeconds() const {
    Impl& d = *m_impl;
    if (!d.plugin || d.sampleRate <= 0) return 0.0;
    auto* lat = (const clap_plugin_latency_t*)d.plugin->get_extension(d.plugin, CLAP_EXT_LATENCY);
    if (!lat) return 0.0;
    return (double)lat->get(d.plugin) / d.sampleRate;
}

// --- state -----------------------------------------------------------------
bool ClapEffectHost::saveState(std::vector<uint8_t>& out) {
    Impl& d = *m_impl;
    if (!d.plugin) return false;
    auto* st = (const clap_plugin_state_t*)d.plugin->get_extension(d.plugin, CLAP_EXT_STATE);
    if (!st) return false;
    out.clear();
    OStreamCtx ctx{ &out };
    clap_ostream_t s{}; s.ctx = &ctx; s.write = ostreamWrite;
    return st->save(d.plugin, &s);
}

bool ClapEffectHost::loadState(const uint8_t* data, size_t size) {
    Impl& d = *m_impl;
    if (!d.plugin || !data || size == 0) return false;
    auto* st = (const clap_plugin_state_t*)d.plugin->get_extension(d.plugin, CLAP_EXT_STATE);
    if (!st) return false;
    IStreamCtx ctx{ data, size, 0 };
    clap_istream_t s{}; s.ctx = &ctx; s.read = istreamRead;
    return st->load(d.plugin, &s);
}

// --- params ----------------------------------------------------------------
uint32_t ClapEffectHost::paramCount() {
    Impl& d = *m_impl;
    if (!d.plugin) return 0;
    auto* pr = (const clap_plugin_params_t*)d.plugin->get_extension(d.plugin, CLAP_EXT_PARAMS);
    return pr ? pr->count(d.plugin) : 0;
}

bool ClapEffectHost::paramInfoByIndex(uint32_t index, ClapParamInfo& out) {
    Impl& d = *m_impl;
    if (!d.plugin) return false;
    auto* pr = (const clap_plugin_params_t*)d.plugin->get_extension(d.plugin, CLAP_EXT_PARAMS);
    if (!pr) return false;
    clap_param_info_t info{};
    if (!pr->get_info(d.plugin, index, &info)) return false;
    out.id = info.id;
    out.name = info.name;
    out.minValue = info.min_value;
    out.maxValue = info.max_value;
    out.defaultValue = info.default_value;
    out.isStepped = (info.flags & CLAP_PARAM_IS_STEPPED) != 0;
    return true;
}

double ClapEffectHost::paramValue(clap_id id) {
    Impl& d = *m_impl;
    if (!d.plugin) return 0.0;
    auto* pr = (const clap_plugin_params_t*)d.plugin->get_extension(d.plugin, CLAP_EXT_PARAMS);
    double v = 0.0;
    if (pr) pr->get_value(d.plugin, id, &v);
    return v;
}

bool ClapEffectHost::paramValueToText(clap_id id, double value, std::string& out) {
    Impl& d = *m_impl;
    if (!d.plugin) return false;
    auto* pr = (const clap_plugin_params_t*)d.plugin->get_extension(d.plugin, CLAP_EXT_PARAMS);
    if (!pr) return false;
    char buf[CLAP_NAME_SIZE * 2] = {0};
    if (!pr->value_to_text(d.plugin, id, value, buf, sizeof(buf))) return false;
    out = buf;
    return true;
}

void ClapEffectHost::setParamAndFlush(clap_id id, double value) {
    Impl& d = *m_impl;
    if (!d.plugin) return;
    clap_event_param_value_t e{};
    e.header.size = sizeof(e);
    e.header.type = CLAP_EVENT_PARAM_VALUE;
    e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    e.header.flags = 0;
    e.param_id = id;
    e.cookie = nullptr;
    e.note_id = -1;
    e.port_index = -1;
    e.channel = -1;
    e.key = -1;
    e.value = value;
    {
        std::lock_guard<std::mutex> lk(d.paramMutex);
        d.pendingParams.push_back(e);
    }
    // Apply immediately via a short silent block (main-thread editing path).
    if (d.active && d.processing) {
        std::vector<float> silent((size_t)64 * 2, 0.f);
        d.processBlock(silent.data(), 64, 2);
    }
    if (d.callbackRequested.exchange(false)) d.plugin->on_main_thread(d.plugin);
}

// --- preset-load -----------------------------------------------------------
bool ClapEffectHost::loadPreset(uint32_t locationKind, const std::string& location,
                                const std::string& loadKey) {
    Impl& d = *m_impl;
    if (!d.plugin) return false;
    auto* pl = (const clap_plugin_preset_load_t*)d.plugin->get_extension(d.plugin, CLAP_EXT_PRESET_LOAD);
    if (!pl) pl = (const clap_plugin_preset_load_t*)d.plugin->get_extension(d.plugin, CLAP_EXT_PRESET_LOAD_COMPAT);
    if (!pl) { console::warning("foo_dsp_clap: plugin has no preset-load extension"); return false; }
    const char* loc = location.empty() ? nullptr : location.c_str();
    const char* key = loadKey.empty() ? nullptr : loadKey.c_str();
    return pl->from_location(d.plugin, locationKind, loc, key);
}

// --- gui -------------------------------------------------------------------
bool ClapEffectHost::guiSupportsEmbeddedCocoa() {
    Impl& d = *m_impl;
    if (!d.plugin) return false;
    auto* gui = (const clap_plugin_gui_t*)d.plugin->get_extension(d.plugin, CLAP_EXT_GUI);
    return gui && gui->is_api_supported(d.plugin, CLAP_WINDOW_API_COCOA, false);
}

bool ClapEffectHost::guiCreateEmbedded(void* parentNSView, uint32_t* outW, uint32_t* outH) {
    Impl& d = *m_impl;
    if (!d.plugin) return false;
    auto* gui = (const clap_plugin_gui_t*)d.plugin->get_extension(d.plugin, CLAP_EXT_GUI);
    if (!gui) return false;
    if (d.guiCreated) { gui->destroy(d.plugin); d.guiCreated = false; }
    if (!gui->create(d.plugin, CLAP_WINDOW_API_COCOA, false)) return false;
    d.guiCreated = true;

    uint32_t w = 0, h = 0;
    gui->get_size(d.plugin, &w, &h);

    clap_window_t win{};
    win.api = CLAP_WINDOW_API_COCOA;
    win.cocoa = parentNSView;
    if (!gui->set_parent(d.plugin, &win)) { gui->destroy(d.plugin); d.guiCreated = false; return false; }
    gui->show(d.plugin);

    if (outW) *outW = w;
    if (outH) *outH = h;
    return true;
}

bool ClapEffectHost::guiCanResize() {
    Impl& d = *m_impl;
    if (!d.plugin) return false;
    auto* gui = (const clap_plugin_gui_t*)d.plugin->get_extension(d.plugin, CLAP_EXT_GUI);
    return gui && gui->can_resize(d.plugin);
}

void ClapEffectHost::guiDestroy() {
    Impl& d = *m_impl;
    if (!d.plugin || !d.guiCreated) return;
    auto* gui = (const clap_plugin_gui_t*)d.plugin->get_extension(d.plugin, CLAP_EXT_GUI);
    if (gui) gui->destroy(d.plugin);
    d.guiCreated = false;
}

void ClapEffectHost::setResizeCallback(std::function<void(uint32_t, uint32_t)> cb) {
    m_impl->resizeCb = std::move(cb);
}

} // namespace foo_clap_dsp
