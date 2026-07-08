cmake_minimum_required(VERSION 3.10)

# set the project name
project(musiceditor VERSION 0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native -Wall -Wsuggest-override -Werror=return-local-addr -Werror=multichar -Werror=enum-compare -Werror=return-type -Werror=conversion-null -Werror=parentheses -Werror=switch -Werror=address -Werror=trigraphs -Werror=pointer-arith -Werror=cast-qual -Werror=non-virtual-dtor -ffast-math -fno-math-errno -Werror=write-strings -Werror=return-local-addr -Wuninitialized -Wno-suggest-override -Wsign-conversion -Wsign-promo")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pg")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pg")

# -Wconversion

# add the executable
add_executable(musiceditor main.cpp
	UI.cpp
	AlsaAudio.cpp
	Oscilator.cpp
	Noise.cpp
	FileInstrument.cpp
	TerminalUI.cpp
	Controller.cpp
	InstrumentList.cpp
	SoundFont.cpp
	Song.cpp
	SongObject.cpp
	InstrumentTrack.cpp
	HierarchyView.cpp
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
	PatternEditor.cpp
	NoteMultiplier.cpp
	HarmonicSeries.cpp
	Arpeggiator.cpp
	LFO.cpp
)

target_link_libraries(musiceditor m asound sndfile notcurses++ notcurses notcurses-core fmt fftw3 pthread)
