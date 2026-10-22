set shell := ["bash", "-euo", "pipefail", "-c"]
set windows-shell := ["sh", "-euo", "pipefail", "-c"]

name := "openkick"
version := trim(read("VERSION"))

build := "build"
dist := "dist"
generated := build / "generated"

hostCC := env("CC", if os() == "windows" { "gcc" } else { "cc" })
mingwCC := env("MINGW_CC", if os() == "windows" { "gcc" } else { "x86_64-w64-mingw32-gcc" })
clangFormat := env("CLANG_FORMAT", "clang-format")
extraFlags := env("OPENKICK_CFLAGS", "")

includes := "-Isrc -Ithird_party/vst3_c_api -Ithird_party/stb"
warnings := "-Wall -Wextra -Wshadow -Wstrict-prototypes -Wmissing-prototypes"
cflags := "-std=c11 -O2 " + warnings + " " + includes + " -DOPENKICK_VERSION='\"" + version + "\"' " + extraFlags

coreSources := "src/params.c src/dsp/shaper.c src/ui/canvas.c src/ui/font.c src/ui/editor.c " + generated / "fonts.c"
vst3Sources := "src/vst3/plugin.c src/vst3/component.c src/vst3/processor.c src/vst3/controller.c src/vst3/view.c src/vst3/factory.c"
sourceFiles := "src/*.c src/*.h src/*/*.c src/*/*.h src/platform/*.m tests/*.c tools/*.c"

macBundle := build / "macos" / name + ".vst3"
macBinary := macBundle / "Contents/MacOS" / name
macArchs := "-arch arm64 -arch x86_64 -mmacosx-version-min=10.13"

winBundle := build / "windows" / name + ".vst3"
winBinary := winBundle / "Contents/x86_64-win" / name + ".vst3"

# List the available recipes
default:
    @just --list --unsorted

# Build the plugin for the current platform
[macos]
build: build-macos

# Build the plugin for the current platform
[windows]
build: build-windows

# Run every check: formatting, strict build, tests and sanitizers
[macos]
check: format-check strict test sanitize

# Run every check: strict build and tests
[windows]
check: strict test

# Build the universal macOS bundle (arm64 + x86_64)
build-macos: fonts
    mkdir -p {{macBundle}}/Contents/MacOS {{build}}/objects/macos
    {{hostCC}} {{macArchs}} {{cflags}} -fvisibility=hidden -c src/platform/macos.m -fobjc-arc \
        -o {{build}}/objects/macos/platform.o
    {{hostCC}} {{macArchs}} {{cflags}} -fvisibility=hidden -bundle -framework Cocoa \
        {{coreSources}} {{vst3Sources}} {{build}}/objects/macos/platform.o -o {{macBinary}}
    sed 's/@VERSION@/{{version}}/g' resources/macos/Info.plist.in > {{macBundle}}/Contents/Info.plist
    just _moduleinfo {{macBundle}}
    printf 'BNDL????' > {{macBundle}}/Contents/PkgInfo
    codesign --force --sign - {{macBundle}}
    @echo "built {{macBundle}}"

# Build the Windows x64 bundle (natively, or cross-compiled with mingw-w64)
build-windows: fonts
    mkdir -p "$(dirname {{winBinary}})"
    {{mingwCC}} -shared {{cflags}} {{coreSources}} {{vst3Sources}} src/platform/windows.c \
        -o {{winBinary}} -static -static-libgcc -lgdi32 -luser32
    just _moduleinfo {{winBundle}}
    @echo "built {{winBundle}}"

# Rebuild with warnings treated as errors
strict:
    OPENKICK_CFLAGS=-Werror just build

# Load the plugin in the offline test host and render the editor
[macos]
test: build-macos (test-host macBinary) snapshot

# Load the plugin in the offline test host and render the editor
[windows]
test: build-windows (test-host winBinary) snapshot

[private]
test-host binary:
    mkdir -p {{build}}/tests
    {{hostCC}} {{cflags}} tests/host.c src/params.c src/dsp/shaper.c -o {{build}}/tests/host -lm
    {{build}}/tests/host {{binary}}

# Render the editor offscreen to build/openkick.png
snapshot: fonts
    mkdir -p {{build}}/tests
    {{hostCC}} {{cflags}} -Wno-missing-prototypes tests/snapshot.c {{coreSources}} -o {{build}}/tests/snapshot -lm
    {{build}}/tests/snapshot {{build}}/openkick.png 2

# Run the test host against a build instrumented with AddressSanitizer and UBSan
[macos]
sanitize: fonts
    mkdir -p {{build}}/sanitize
    {{hostCC}} {{cflags}} -g -fsanitize=address,undefined -fno-omit-frame-pointer -fvisibility=hidden -bundle \
        -fobjc-arc -framework Cocoa {{coreSources}} {{vst3Sources}} src/platform/macos.m -o {{build}}/sanitize/{{name}}
    {{hostCC}} {{cflags}} -g -fsanitize=address,undefined tests/host.c src/params.c src/dsp/shaper.c \
        -o {{build}}/sanitize/host
    {{build}}/sanitize/host {{build}}/sanitize/{{name}}

# Update assets/openkick.png, the screenshot used by the README
screenshot: snapshot
    cp {{build}}/openkick.png assets/openkick.png

# Format every source file in place
format:
    {{clangFormat}} -i {{sourceFiles}}

# Fail if any source file is not formatted
format-check:
    {{clangFormat}} --dry-run --Werror {{sourceFiles}}

# Package the macOS release zip
package-macos: build-macos
    just _package macOS {{macBundle}}

# Package the Windows release zip
package-windows: build-windows
    just _package Windows {{winBundle}}

_moduleinfo bundle:
    mkdir -p {{bundle}}/Contents/Resources
    sed 's/@VERSION@/{{version}}/g' resources/moduleinfo.json.in > {{bundle}}/Contents/Resources/moduleinfo.json

_package platform bundle:
    #!/usr/bin/env bash
    set -euo pipefail
    folder="{{name}}-{{version}}-{{platform}}"
    staging="{{build}}/package/$folder"
    archive="$PWD/{{dist}}/$folder.zip"
    rm -rf "$staging" "$archive"
    mkdir -p "$staging" "{{dist}}"
    cp -R "{{bundle}}" "$staging/"
    cp "resources/packaging/INSTALL-{{platform}}.txt" "$staging/INSTALL.txt"
    cp LICENSE "$staging/LICENSE.txt"
    cd "{{build}}/package"
    if command -v ditto >/dev/null; then
        ditto -c -k --norsrc --noextattr --keepParent "$folder" "$archive"
    elif command -v zip >/dev/null; then
        zip -qr "$archive" "$folder"
    else
        powershell -NoProfile -Command "Compress-Archive -Path '$folder' -DestinationPath '$(cygpath -w "$archive")' -Force"
    fi
    echo "packaged $archive"

# Install the plugin system-wide (asks for the administrator password)
[macos]
install: build-macos
    sudo rm -rf /Library/Audio/Plug-Ins/VST3/{{name}}.vst3
    sudo cp -R {{macBundle}} /Library/Audio/Plug-Ins/VST3/

# Install the plugin system-wide (run from an elevated shell)
[windows]
install: build-windows
    rm -rf "/c/Program Files/Common Files/VST3/{{name}}.vst3"
    cp -R {{winBundle}} "/c/Program Files/Common Files/VST3/"

# Remove build outputs and release archives
clean:
    rm -rf {{build}} {{dist}}

[private]
fonts:
    mkdir -p {{generated}}
    {{hostCC}} -O2 tools/embed.c -o {{build}}/embed
    {{build}}/embed {{generated}}/fonts.c \
        fontTitleData=third_party/fonts/InstrumentSerif-Italic.ttf \
        fontDisplayData=third_party/fonts/BebasNeue-Regular.ttf \
        fontMonoData=third_party/fonts/IBMPlexMono-Regular.ttf \
        fontMonoMediumData=third_party/fonts/IBMPlexMono-Medium.ttf
