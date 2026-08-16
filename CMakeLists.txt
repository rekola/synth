cmake_minimum_required(VERSION 3.10)

# set the project name
project(musiceditor VERSION 0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Default to an optimized build when the generator supports build types (a
# plain `cmake -B build` with no CMAKE_BUILD_TYPE otherwise applies no -O
# flag at all - effectively -O0 - which is a real trap for an audio engine:
# it's not just slower, some paths (e.g. AmbisonicBinauralMixer's per-
# sample HRIR convolution) go from comfortably realtime to unusable
# without it). Multi-config generators (Ninja Multi-Config, Visual Studio)
# ignore CMAKE_BUILD_TYPE and already default their own per-config
# optimization, so this is skipped for those.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# "native" (default) squeezes every instruction the *building* machine's own
# CPU supports out of a local dev build - exactly wrong for anything whose
# output needs to reproduce across machines (a CI runner fleet that doesn't
# guarantee the same physical CPU build to build, tests/RenderTests.cpp's
# own golden-render hash test in particular). Override with e.g.
# -DSYNTH_MARCH=x86-64-v2 for a fixed, portable baseline instead - CI does
# this; local dev builds keep the performance-maximizing default.
set(SYNTH_MARCH "native" CACHE STRING "Value passed to -march (e.g. native, x86-64-v2, armv8-a) - a fixed value gives reproducible output across machines at the cost of not using that specific machine's full instruction set; 'native' (default) is the reverse trade-off, right for local dev builds.")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=${SYNTH_MARCH} -Wall -Wsuggest-override -Werror=return-local-addr -Werror=multichar -Werror=enum-compare -Werror=return-type -Werror=conversion-null -Werror=parentheses -Werror=switch -Werror=address -Werror=trigraphs -Werror=pointer-arith -Werror=cast-qual -Werror=non-virtual-dtor -ffast-math -fno-math-errno -Werror=write-strings -Werror=return-local-addr -Wuninitialized -Wno-suggest-override -Werror=sign-conversion -Werror=sign-compare -Werror=sign-promo -Werror=duplicated-cond -Werror=duplicated-branches -Werror=logical-op -Werror=redundant-decls -Werror=cast-align -Wformat=2 -Werror=format -Werror=shadow -Werror=missing-declarations")
# -Wnull-dereference deliberately not enabled: at -O2 with heavy inlining it
# produces false positives inside ordinary std::vector/std::unordered_map
# iteration (GCC misattributes an optimizer-derived diagnostic through the
# inlined STL code, not an issue with our code or a vendored header) -
# confirmed against this codebase/GCC 13 combination, not a hypothetical.

# -Wconversion (broader than -Wsign-conversion - also catches float<->int
# narrowing) is a real candidate for a future pass now that
# -Wsign-conversion itself is clean, but hasn't been measured against this
# codebase yet.

option(SYNTH_ENABLE_PROFILING "Build with -pg (gprof) instrumentation" OFF)
if(SYNTH_ENABLE_PROFILING)
  add_compile_options(-pg)
  add_link_options(-pg)
endif()

option(SYNTH_ENABLE_SANITIZERS "Build with -fsanitize=address,undefined" OFF)
if(SYNTH_ENABLE_SANITIZERS)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# Engine library: song model, playback, instruments and effects. Has no
# dependency on the terminal UI (notcurses) or audio device (ALSA), so it can
# be linked into the test suite and any future offline tooling.
add_library(synth_engine STATIC
	src/instruments/Oscillator.cpp
	src/instruments/Noise.cpp
	src/instruments/FileInstrument.cpp
	src/Controller.cpp
	src/ui/InstrumentList.cpp
	src/instruments/SoundFont.cpp
	src/instruments/SF2Modulator.cpp
	src/model/Song.cpp
	src/model/SongObject.cpp
	src/model/InstrumentTrack.cpp
	third_party/tinyxml2/tinyxml2.cpp
	src/effects/ResonantFilter.cpp
	src/effects/BiquadFilter.cpp
	src/effects/Distortion.cpp
	src/effects/Tremolo.cpp
	src/effects/Chorus.cpp
	src/dsp/ChorusEngine.cpp
	src/dsp/GranularEngine.cpp
	src/bus/BusEffect.cpp
	src/bus/BusEffectRegistry.cpp
	src/bus/FDNReverb.cpp
	src/bus/MultiTapDelay.cpp
	src/bus/GranularCloud.cpp
	src/bus/SendBusProcessor.cpp
	src/bus/Haze.cpp
	src/ambisonic/AmbisonicDiffuseEncoder.cpp
	src/effects/Amplifier.cpp
	src/effects/EnvelopeFilter.cpp
	src/effects/Compressor.cpp
	src/effects/TapeDegradation.cpp
	src/dsp/TapeTransport.cpp
	src/playback/Player.cpp
	src/playback/VisualizationThread.cpp
	src/dsp/DiracAnalyzer.cpp
	src/instruments/NoteMultiplier.cpp
	src/instruments/Arpeggiator.cpp
	src/state/ArpeggiatorState.cpp
	src/instruments/LFO.cpp
	src/audio/OfflineRenderer.cpp
	src/ui/PatternBlockOps.cpp
	src/ui/PatternScroll.cpp
	src/launchpad/LaunchpadLayout.cpp
	src/launchpad/LaunchpadProtocol.cpp
	src/instruments/DrumRankTable.cpp
	src/model/DrumMachineTrack.cpp
	src/ambisonic/MixerFactory.cpp
)
target_include_directories(synth_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# Vendored code (third_party/) as a SYSTEM include path: headers reached
# through it (tinyxml2.h, pocketfft_hdronly.h) are exempted from our own
# warning flags, since we don't reformat/refactor vendored files to chase
# them. tinyxml2.cpp is compiled directly as its own translation unit
# rather than merely #include'd, so the SYSTEM include path doesn't cover
# it - its warnings are separately silenced below.
target_include_directories(synth_engine SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
set_source_files_properties(third_party/tinyxml2/tinyxml2.cpp PROPERTIES COMPILE_OPTIONS "-w")
target_link_libraries(synth_engine PUBLIC m sndfile fmt pthread)

# Experimental, out-of-spec GM-family channel-pressure heuristic defaults
# (SoundFont.cpp - injects a vibrato/filter-cutoff modulator for presets
# that never authored their own channel-pressure connection). Off by
# default: doesn't feel good to play without a pressure-floor fix that
# hasn't landed yet (see plans/launchpad-pressure-floor.md), and isn't
# part of the SF2 spec - real, file-authored channel-pressure modulators
# (SF2Modulator.cpp) always work regardless of this setting. PUBLIC so
# the same macro is visible to tests/SF2ModulatorTests.cpp too (linked
# against synth_engine), which has its own heuristic-specific tests
# guarded the same way.
option(SYNTH_ENABLE_SF2_PRESSURE_HEURISTICS "Enable the experimental GM-family channel-pressure heuristic defaults" OFF)
if(SYNTH_ENABLE_SF2_PRESSURE_HEURISTICS)
  target_compile_definitions(synth_engine PUBLIC SYNTH_ENABLE_SF2_PRESSURE_HEURISTICS)
endif()

# Optional binaural ambisonic decoder: only libmysofa (HRIR loading) is an
# external dependency - the FOA encode/decode math itself
# (AmbisonicEncoding.h) has no dependency of its own. AmbisonicStereoMixer
# (the cardioid stereo decoder) always compiles, no matter this setting.
find_library(MYSOFA_LIBRARY mysofa)
find_path(MYSOFA_INCLUDE_DIR mysofa.h)
if(MYSOFA_LIBRARY AND MYSOFA_INCLUDE_DIR)
  set(SYNTH_ENABLE_BINAURAL_DEFAULT ON)
else()
  set(SYNTH_ENABLE_BINAURAL_DEFAULT OFF)
endif()
option(SYNTH_ENABLE_BINAURAL "Build the libmysofa-based binaural ambisonic decoder" ${SYNTH_ENABLE_BINAURAL_DEFAULT})

if(SYNTH_ENABLE_BINAURAL)
  if(NOT (MYSOFA_LIBRARY AND MYSOFA_INCLUDE_DIR))
    message(FATAL_ERROR "SYNTH_ENABLE_BINAURAL requested but libmysofa was not found (install libmysofa-dev)")
  endif()
  target_sources(synth_engine PRIVATE src/ambisonic/AmbisonicBinauralMixer.cpp src/ambisonic/AmbisonicMagLSDecoder.cpp)
  target_compile_definitions(synth_engine PUBLIC SYNTH_HAVE_LIBMYSOFA)
  target_include_directories(synth_engine PRIVATE ${MYSOFA_INCLUDE_DIR})
  target_link_libraries(synth_engine PUBLIC ${MYSOFA_LIBRARY})
endif()

# Embeds THIRD_PARTY_LICENSES.md into a generated header at build time (not
# read from disk at runtime, so `--licenses` works regardless of the
# binary's working directory/install location - see main.cpp) - the .md
# file stays the single source of truth; this just duplicates its content
# into a form main.cpp can print.
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_LICENSES.md SYNTH_THIRD_PARTY_LICENSES_CONTENT)
configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/ThirdPartyLicenses.h.in
	${CMAKE_CURRENT_BINARY_DIR}/generated/ThirdPartyLicenses.h
	@ONLY
)

# add the executable
add_executable(musiceditor src/main.cpp
	src/ui/UI.cpp
	src/audio/AlsaAudio.cpp
	src/launchpad/LaunchpadIO.cpp
	src/launchpad/LaunchpadManager.cpp
	src/ui/TerminalUI.cpp
	src/ui/HierarchyView.cpp
	src/ui/PatternEditor.cpp
)
target_include_directories(musiceditor PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(musiceditor synth_engine asound notcurses++ notcurses notcurses-core)

enable_testing()
add_subdirectory(tests)
