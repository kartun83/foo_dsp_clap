# foo_dsp_clap

A foobar2000 (macOS, Apple Silicon) **DSP** that runs **CLAP audio-effect**
plugins installed on your system on the playback stream — with a GUI to control
the plugin and browse its presets.

It's the effect-side companion to
[foo_tun_midi](https://github.com/kartun83/foo_tun_midi), which hosts CLAP
*instruments* for MIDI playback; the two share the same CLAP host code.

## What it does

- Adds a **"CLAP Effect"** DSP to Preferences → Playback → DSP Manager.
- Its **Configure** dialog lets you:
  - pick a CLAP audio effect (scanned from `~/Library/Audio/Plug-Ins/CLAP` and
    `/Library/Audio/Plug-Ins/CLAP`),
  - browse and load the plugin's presets (when the plugin exposes them to hosts),
  - open the plugin's **own native GUI** in a window — or, for plugins without an
    embeddable GUI, tweak auto-generated parameter sliders.
- The full plugin state is saved into the foobar DSP preset, so it persists across
  restarts and travels with your DSP presets.

## Build

```sh
# Fetch + build the foobar2000 SDK (needs: brew install sevenzip)
./Scripts/bootstrap_sdk.sh
# ...or reuse an already-built SDK (e.g. the sibling foo_tun_midi checkout):
export FB2K_SDK_PATH=/path/to/SDK-2025-03-07

./Scripts/build.sh --regenerate --install
```

Restart foobar2000 to load the component.

## Requirements & limitations

- macOS on Apple Silicon (arm64-only), foobar2000 v2.
- **JIT-compiling CLAP effects can crash foobar2000** (its hardened runtime lacks
  `com.apple.security.cs.allow-jit`) — same known limitation as foo_tun_midi.
  Prefer non-JIT effects. See `docs/ARCHITECTURE.md`.
- Rescanning the plugin folder raises memory until the next restart (macOS never
  unloads a plugin image once dlopen'd). Playback itself never scans.
- Stereo/mono streams are processed; wider streams have their first two channels
  processed and the rest passed through.

## License

MIT — see `LICENSE`. See `ACKNOWLEDGMENTS.md` for upstream credits.
