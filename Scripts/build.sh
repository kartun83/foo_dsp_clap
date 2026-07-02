#!/bin/bash
#
# build.sh - Build the foo_dsp_clap component
#
# A foobar2000 DSP that hosts CLAP audio-effect plugins installed on the system.
#
# Usage:
#   ./Scripts/build.sh [OPTIONS]
#
# Options:
#   --debug       Build Debug configuration (default: Release)
#   --release     Build Release configuration
#   --clean       Clean before building
#   --regenerate  Regenerate Xcode project before building
#   --install     Install to foobar2000 after building
#   --package     Zip the built .component into a .fb2k-component
#   --help        Show this help message

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

PROJECT_NAME="foo_dsp_clap"
source "$SCRIPT_DIR/lib.sh"

DO_INSTALL=false
DO_PACKAGE=false
DO_CLEAN=false
REGENERATE=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --debug)      BUILD_CONFIG="Debug"; shift ;;
        --release)    BUILD_CONFIG="Release"; shift ;;
        --clean)      DO_CLEAN=true; shift ;;
        --regenerate) REGENERATE=true; shift ;;
        --install)    DO_INSTALL=true; shift ;;
        --package)    DO_PACKAGE=true; shift ;;
        --help|-h)    head -19 "$0" | tail -16; exit 0 ;;
        *)            print_error "Unknown option: $1"; exit 1 ;;
    esac
done

# Recompute paths with the possibly-updated BUILD_CONFIG.
COMPONENT_PATH="$BUILD_DIR/$BUILD_CONFIG/$PROJECT_NAME.component"
DEST_PATH="$COMPONENT_FOLDER/$PROJECT_NAME.component"

do_package() {
    local src="$COMPONENT_PATH"
    local out="$BUILD_DIR/$BUILD_CONFIG/$PROJECT_NAME.fb2k-component"
    if [ ! -d "$src" ]; then
        print_error "Cannot package: $src not found"; return 1
    fi
    rm -f "$out"
    ( cd "$(dirname "$src")" && zip -r -X -q "$out" "$(basename "$src")" )
    print_success "Packaged: $out"
}

BUILD_ARGS=()
[ "$DO_CLEAN" = true ] && BUILD_ARGS+=(--clean)
[ "$REGENERATE" = true ] && BUILD_ARGS+=(--regenerate)

do_build "${BUILD_ARGS[@]}"
[ "$DO_PACKAGE" = true ] && do_package
[ "$DO_INSTALL" = true ] && do_install

print_success "Done."
