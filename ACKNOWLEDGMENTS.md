# Acknowledgments

foo_dsp_clap builds on the work of others:

- **foobar2000 SDK** (© Peter Pawlowski) — the component/DSP API. Fetched at build
  time by `Scripts/bootstrap_sdk.sh`; not redistributed here.
- **CLAP** — CLever Audio Plugin API (https://cleveraudio.org,
  https://github.com/free-audio/clap), MIT-licensed. Headers vendored under
  `third_party/clap-headers/`.
- **foo_tun_midi** (© Alexey Tveritinov, MIT) — the CLAP host, scanner and
  headless preset-discovery code is shared with / adapted from that project.
- **JendaT/fb2k-components-mac-suite** (MIT) — the project structure and build
  tooling (`Scripts/lib.sh`, the Xcode-project generator pattern).

The AU/VST effect hosting in foobar2000 for macOS is closed-source; no code from
it is used here. CLAP was chosen precisely because it is an open standard that can
be hosted from public headers.
