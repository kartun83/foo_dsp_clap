//
//  branding.h
//  foo_dsp_clap
//
//  About-box branding. The macro structure is adapted from
//  JendaT/fb2k-components-mac-suite (shared/common_about.h, MIT) via foo_tun_midi.
//

#pragma once

#define CLAP_DSP_AUTHOR "Alexey Tveritinov"
#define CLAP_DSP_GITHUB_URL "https://github.com/kartun83/foo_dsp_clap"
#define CLAP_DSP_COPYRIGHT_YEAR "2026"

//
// DECLARE_COMPONENT_VERSION with this component's branding and acknowledgments.
//
// Usage:
//   CLAP_DSP_COMPONENT_ABOUT(
//       "CLAP Effect",
//       CLAP_DSP_VERSION,
//       "Description...");
//
#define CLAP_DSP_COMPONENT_ABOUT(name, version, description) \
    DECLARE_COMPONENT_VERSION(name, version, \
        description "\n\n" \
        "Author: " CLAP_DSP_AUTHOR "\n" \
        "Source: " CLAP_DSP_GITHUB_URL "\n" \
        "Copyright (c) " CLAP_DSP_COPYRIGHT_YEAR " " CLAP_DSP_AUTHOR "\n\n" \
        "Hosts CLAP audio-effect plugins (https://cleveraudio.org, MIT).\n" \
        "CLAP host code shared with foo_tun_midi. Project structure and build\n" \
        "tooling adapted from JendaT/fb2k-components-mac-suite (MIT).")
