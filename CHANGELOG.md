# Changelog

All notable changes to foo_dsp_clap are documented here.

## [0.1.0] - 2026-07-02

Initial release.

- foobar2000 DSP (`CLAP Effect`) that hosts a CLAP audio-effect plugin on the
  playback stream.
- Config popup: plugin picker (scanned CLAP effects + Rescan), headless preset
  browser, native plugin GUI in a window, and an auto-generated parameter-slider
  fallback for plugins without an embeddable GUI.
- Full `clap.state` is serialized into the DSP preset for faithful, persistent
  restore.
- Live reconfigure (`dsp_v3`): same-plugin state changes apply without recreating
  the DSP.
- CLAP host / scanner / preset-discovery code shared with foo_tun_midi.
- arm64-only; documents the JIT-plugin crash limitation.
