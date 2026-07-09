cmake_minimum_required(VERSION 3.10)

# set the project name
project(musiceditor VERSION 0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

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
	Song.cpp
	SongObject.cpp
	InstrumentTrack.cpp
	tinyxml2.cpp
	effects/ResonantFilter.cpp
	effects/BiquadFilter.cpp
	effects/Delay.cpp
	effects/Distortion.cpp
	effects/Tremolo.cpp
	effects/Downmix.cpp
	effects/Reverb.cpp
	effects/Chorus.cpp
	effects/Amplifier.cpp
	effects/EnvelopeFilter.cpp
	effects/Compressor.cpp
	Player.cpp
	NoteMultiplier.cpp
	HarmonicSeries.cpp
	Arpeggiator.cpp
	LFO.cpp
	OfflineRenderer.cpp
	PatternBlockOps.cpp
)
target_include_directories(synth_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(synth_engine PUBLIC m sndfile fmt fftw3 pthread)

# add the executable
add_executable(musiceditor main.cpp
	UI.cpp
	AlsaAudio.cpp
	TerminalUI.cpp
	HierarchyView.cpp
	PatternEditor.cpp
)

target_link_libraries(musiceditor synth_engine asound notcurses++ notcurses notcurses-core)

enable_testing()
add_subdirectory(tests)
