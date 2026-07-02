//
//  Main.cpp
//  foo_dsp_clap
//
//  Component registration.
//

#include "../fb2k_sdk.h"
#include "../branding.h"
#include "../version.h"

CLAP_DSP_COMPONENT_ABOUT(
    "CLAP Effect",
    CLAP_DSP_VERSION,
    "A DSP that runs CLAP audio-effect plugins installed on your system on the\n"
    "playback stream. Add it in Preferences > Playback > DSP Manager, then use\n"
    "its configuration dialog to pick a plugin, browse presets, and open the\n"
    "plugin's own GUI.\n\n"
    "Scans ~/Library/Audio/Plug-Ins/CLAP and /Library/Audio/Plug-Ins/CLAP."
);

VALIDATE_COMPONENT_FILENAME("foo_dsp_clap.component");
