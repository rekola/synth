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

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native -Wall -Wsuggest-override -Werror=return-local-addr -Werror=multichar -Werror=enum-compare -Werror=return-type -Werror=conversion-null -Werror=parentheses -Werror=switch -Werror=address -Werror=trigraphs -Werror=pointer-arith -Werror=cast-qual -Werror=non-virtual-dtor -ffast-math -fno-math-errno -Werror=write-strings -Werror=return-local-addr -Wuninitialized -Wno-suggest-override -Wsign-conversion -Wsign-promo")

# -Wconversion

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
	Oscilator.cpp
	Noise.cpp
	FileInstrument.cpp
	Controller.cpp
	InstrumentList.cpp
	SoundFont.cpp
	SF2Modulator.cpp
	Song.cpp
	SongObject.cpp
	InstrumentTrack.cpp
	third_party/tinyxml2/tinyxml2.cpp
	effects/ResonantFilter.cpp
	effects/BiquadFilter.cpp
	effects/Distortion.cpp
	effects/Tremolo.cpp
	effects/Chorus.cpp
	dsp/ChorusEngine.cpp
	dsp/GranularEngine.cpp
	bus/BusEffect.cpp
	bus/BusEffectRegistry.cpp
	bus/FDNReverb.cpp
	bus/MultiTapDelay.cpp
	bus/GranularCloud.cpp
	bus/SendBusProcessor.cpp
	bus/Haze.cpp
	AmbisonicDiffuseEncoder.cpp
	effects/Amplifier.cpp
	effects/EnvelopeFilter.cpp
	effects/Compressor.cpp
	Player.cpp
	VisualizationThread.cpp
	dsp/DiracAnalyzer.cpp
	NoteMultiplier.cpp
	Arpeggiator.cpp
	LFO.cpp
	OfflineRenderer.cpp
	PatternBlockOps.cpp
	PatternScroll.cpp
	LaunchpadLayout.cpp
	LaunchpadProtocol.cpp
	DrumRankTable.cpp
	DrumMachineTrack.cpp
	MixerFactory.cpp
)
target_include_directories(synth_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
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
  target_sources(synth_engine PRIVATE AmbisonicBinauralMixer.cpp AmbisonicMagLSDecoder.cpp)
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
add_executable(musiceditor main.cpp
	UI.cpp
	AlsaAudio.cpp
	LaunchpadIO.cpp
	LaunchpadManager.cpp
	TerminalUI.cpp
	HierarchyView.cpp
	PatternEditor.cpp
)
target_include_directories(musiceditor PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(musiceditor synth_engine asound notcurses++ notcurses notcurses-core)

enable_testing()
add_subdirectory(tests)
