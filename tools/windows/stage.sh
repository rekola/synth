#!/bin/bash
# Lays out what the installer ships: the program, the MSYS2 runtime DLLs it
# needs, the songs, the licence texts, a SOFA file when the build has the
# binaural decoder and, if given, SoundFonts.
# Usage (MSYS2 UCRT64 shell, from the repository root):
#   tools/windows/stage.sh BUILD_DIR OUT_DIR [SOUNDFONT_DIR]
# The layout is what util/Paths.cpp looks for: share/ next to synth.exe.
set -euo pipefail

build=$1
out=$2
soundfonts=${3:-}

rm -rf "$out"
mkdir -p "$out/share/synth/songs"
cp "$build/synth.exe" "$out/"

# The DLLs from the toolchain, and theirs in turn, until nothing new turns up.
copied=1
while [ "$copied" -gt 0 ]; do
  copied=0
  for file in "$out"/*.exe "$out"/*.dll; do
    [ -e "$file" ] || continue
    for dll in $(ldd "$file" | awk '/\/ucrt64\// { print $3 }'); do
      if [ ! -e "$out/$(basename "$dll")" ]; then
        cp "$dll" "$out/"
        copied=1
      fi
    done
  done
done

cp -r songs/. "$out/share/synth/songs/"
cp LICENSE "$out/LICENSE.txt"
cp THIRD_PARTY_LICENSES.md "$out/"

# A SOFA file for binaural decoding, if this build has the decoder.
if grep -q '^SYNTH_ENABLE_BINAURAL:BOOL=ON' "$build/CMakeCache.txt"; then
  mkdir -p "$out/share/sofa"
  cp third_party/sofa/MIT_KEMAR_normal_pinna.sofa third_party/sofa/LICENSE "$out/share/sofa/"
fi

if [ -n "$soundfonts" ] && ls "$soundfonts"/*.sf2 >/dev/null 2>&1; then
  mkdir -p "$out/share/soundfonts"
  cp "$soundfonts"/* "$out/share/soundfonts/"
fi

echo "Staged in $out:"
ls "$out"
