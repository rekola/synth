add_executable(synth_tests
	test_main.cpp
	AudioBufferTests.cpp
	ChannelConfigurationTests.cpp
	TunerTests.cpp
	RenderTests.cpp
	ControllerTests.cpp
	PatternBlockOpsTests.cpp
	PatternScrollTests.cpp
	PatternBreakTests.cpp
	CommandTests.cpp
	AzimuthSlideTests.cpp
	SendLiveUpdateTests.cpp
	SongTests.cpp
	LaunchpadLayoutTests.cpp
	LaunchpadProtocolTests.cpp
	LaunchpadTimingTests.cpp
	DrumRankTableTests.cpp
	DrumMachineTrackTests.cpp
	PercussionTrackTests.cpp
	SF2ModulatorTests.cpp
	EnvelopeFilterTests.cpp
	VoiceLoudnessTests.cpp
	AmbisonicEncodingTests.cpp
	AmbisonicMagLSDecoderTests.cpp
	ChorusEngineTests.cpp
	FloorReflectionTests.cpp
	NoiseGeneratorTests.cpp
	FDNReverbTests.cpp
	MultiTapDelayTests.cpp
	BusEffectRegistryTests.cpp
	GranularEngineTests.cpp
	GranularCloudTests.cpp
	VisualizationThreadTests.cpp
	DiracAnalyzerTests.cpp
	HalfbandFilterTests.cpp
	AmbisonicDiffuseEncoderTests.cpp
	SendBusProcessorTests.cpp
	HazeTests.cpp
	ArpeggiatorStateTests.cpp
	ResyncPlayheadTests.cpp
	FractionalDelayLineTests.cpp
	TapeTransportTests.cpp
	TapeVoiceLifecycleTests.cpp
	HashFieldTests.cpp
)

target_link_libraries(synth_tests synth_engine)
target_compile_definitions(synth_tests PRIVATE
	TESTS_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
	TESTS_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}"
	SYNTH_MARCH="${SYNTH_MARCH}")

add_test(NAME synth_tests COMMAND synth_tests)

# Guards against a reintroduced rand()/srand()/non-portable <random>
# distribution anywhere in the tree - see the script's own doc comment.
# Registered as its own CTest test (not folded into synth_tests above)
# since it's a plain source-tree scan with no need for the actual test
# binary at all - it runs even if the C++ build itself is broken.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME no_banned_randomness
	   COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/check_no_banned_randomness.py)
endif()
