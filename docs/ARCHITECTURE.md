# Architecture

foo_dsp_clap hosts a single CLAP **audio-effect** plugin as a foobar2000 DSP.

## Components

- `src/Integration/ClapDsp.cpp` — the `dsp` service (`clap_dsp :
  dsp_impl_base_t<dsp_v3>`, registered via `dsp_factory_t`). `on_chunk` converts
  the interleaved `audio_chunk` to float, runs it through the host, and writes it
  back. Lazily loads + activates the plugin on the first chunk (needs the stream
  sample rate) and re-activates when the rate changes. `flush()` resets the
  plugin after a seek. `apply_preset()` (dsp_v3) applies a new state in place when
  the plugin is unchanged, else returns false so the DSP manager recreates it.
- `src/Clap/ClapEffectHost.{h,cpp}` — the CLAP host. Load/activate/process plus
  the `clap.state`, `clap.params`, `clap.gui`, `clap.latency` and `clap.preset-load`
  extensions. Ported from foo_tun_midi's `ClapRenderer`; keeps its load-bearing
  findings (zero a real buffer for *every* declared audio port, inputs included;
  capture output port 0). Processes in ≤4096-frame sub-blocks.
- `src/Clap/ClapScanner.{h,cpp}` — descriptor-only scan of the standard CLAP
  folders, kept to plugins advertising `CLAP_PLUGIN_FEATURE_AUDIO_EFFECT`. Cached
  + persisted via `ClapConfig`.
- `src/Clap/ClapPresets.{h,cpp}` — headless preset-discovery (enumerate a
  plugin's presets without instantiating it). Shared with foo_tun_midi.
- `src/Core/ClapDspConfig.{h,cpp}` — encodes/decodes the DSP preset blob
  `{version, pluginPath, pluginId, presetName, clapStateBlob}`.
- `src/UI/ClapDspView.mm` — the Cocoa config popup (built programmatically),
  returned from `g_show_config_popup` and presented by foobar as an
  `NSViewController`.

## Where the plugin instances live

There are **two** plugin instances, by design:

1. The **audio DSP** instance (in `clap_dsp`) processes playback. It never shows a
   GUI.
2. The **config popup** creates its own editing instance to drive the native GUI
   and the generic sliders.

The popup captures the editing instance's full `clap.state` (on a ~0.4 s timer and
on explicit actions) and pushes it to the host via
`dsp_preset_edit_callback_v2::set_preset`. foobar then applies it to the audio DSP
(`apply_preset` in place when the plugin is unchanged, or by recreating it). This
avoids sharing one plugin instance across threads while still giving live-ish
control and faithful persistence.

## Threading

- `on_chunk` / `reset` run on foobar's streaming thread. All host/plugin calls
  from `clap_dsp` are serialized by a per-instance mutex shared with
  `apply_preset`.
- The popup uses the host on the main thread only (satisfies CLAP's `[main-thread]`
  contract for gui/state/param edits).
- CLAP marks `activate`/`deactivate` `[main-thread]`; like foo_tun_midi's offline
  renderer we call them from the streaming thread, which works with the real
  plugins tested. Documented as a known deviation.

## Known limitation: JIT-compiling plugins crash foobar2000

foobar2000.app's hardened runtime lacks `com.apple.security.cs.allow-jit`. CLAP
plugins that JIT at runtime (e.g. DSP56300 emulators / asmjit-based plugins) trap
in `pthread_jit_write_protect_np` and take the whole process down — uncatchable
in-process and not detectable from the descriptor, so the scanner can't pre-filter
them. Prefer non-JIT effects. A user may re-sign foobar2000 with an entitlements
plist adding `com.apple.security.cs.allow-jit`:

```sh
codesign --sign - --force --options runtime \
  --entitlements allow-jit.entitlements /Applications/foobar2000.app
```

(unsupported; do at your own risk).
