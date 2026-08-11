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
)

target_link_libraries(synth_tests synth_engine)
target_compile_definitions(synth_tests PRIVATE
	TESTS_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
	TESTS_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}")

add_test(NAME synth_tests COMMAND synth_tests)
