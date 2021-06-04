cmake_minimum_required(VERSION 3.10)

# set the project name
project(musiceditor VERSION 0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# add the executable
add_executable(musiceditor main.cpp
	UI.cpp
	AlsaAudio.cpp
	SubtractiveInstrument.cpp
	FileInstrument.cpp
	TerminalUI.cpp
	Chart.cpp
	Controller.cpp
	FMInstrument.cpp
	InstrumentList.cpp
	SoundFont.cpp
	Song.cpp
	HRFT.cpp
	Track.cpp
	InstrumentTrack.cpp
	tinyxml2.cpp
	Filter.cpp
	Compressor.cpp
	Delay.cpp
	Distortion.cpp
	Reverb.cpp
	Player.cpp
	PatternEditor.cpp
	Effect.cpp
	Chorus.cpp
)

target_link_libraries(musiceditor m asound sndfile notcurses++ notcurses notcurses-core fmt fftw3 spatialaudio pthread)
