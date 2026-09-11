add_executable(synth_tests
	test_main.cpp
	# Compiled directly as an extra source here (mirroring how
	# TerminalUI.cpp itself is only ever added straight to the `synth`
	# executable, never into synth_engine) rather than folded into
	# synth_engine's own library composition: EscapeCoalescer.h/.cpp only
	# needs notcurses's headers (types, macros, static inline helpers), not
	# the linked library, so this keeps that distinction visible at the
	# build-system level too - see EscapeCoalescer.h's own top comment.
	../src/ui/tui/EscapeCoalescer.cpp
	EscapeCoalescerTests.cpp
	# Same reasoning as EscapeCoalescer.cpp above - SubcellGlyphs.cpp has no
	# notcurses dependency of its own at all (plain integer/table logic),
	# so it's an even simpler case for direct inclusion here rather than
	# folding into synth_engine.
	../src/ui/tui/SubcellGlyphs.cpp
	SubcellGlyphsTests.cpp
	AudioBufferTests.cpp
	ChannelConfigurationTests.cpp
	TunerTests.cpp
	RenderTests.cpp
	ControllerTests.cpp
	PatternBlockOpsTests.cpp
	ArrangementOpsTests.cpp
	SectionTests.cpp
	PatternScrollTests.cpp
	PatternBreakTests.cpp
	CommandTests.cpp
	AzimuthSlideTests.cpp
	SendSetCommandTests.cpp
	SendLiveUpdateTests.cpp
	SongTests.cpp
	LaunchpadLayoutTests.cpp
	LaunchpadProtocolTests.cpp
	LaunchpadTimingTests.cpp
	DrumRankTableTests.cpp
	PercussionTrackTests.cpp
	SF2ModulatorTests.cpp
	InstrumentResolverTests.cpp
	GeneratorOverrideTests.cpp
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
	BusEffectLiveUpdateTests.cpp
	HazeTests.cpp
	ArpeggiatorStateTests.cpp
	ResyncPlayheadTests.cpp
	FractionalDelayLineTests.cpp
	TapeTransportTests.cpp
	TapeVoiceLifecycleTests.cpp
	HashFieldTests.cpp
	PlayerMultiBufferTests.cpp
	Utf8Tests.cpp
	SongStructureTests.cpp
	SampleTrackTests.cpp
	TimeStretcherTests.cpp
	RecordingRingBufferTests.cpp
	GroovePatternLibraryTests.cpp
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
