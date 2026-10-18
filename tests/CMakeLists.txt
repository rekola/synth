add_executable(synth_tests
	test_main.cpp
	DocumentTests.cpp
	SongEditTests.cpp
	ContentPublishingTests.cpp
	SongDocumentTests.cpp
	UndoTests.cpp
	TrackDocumentTests.cpp
	PatternViewTests.cpp
	ClipArrangementViewTests.cpp
	MetronomeTests.cpp
	AudioBufferTests.cpp
	ChannelBankTests.cpp
	ChannelConfigurationTests.cpp
	TunerTests.cpp
	RenderTests.cpp
	FMTests.cpp
	ControllerTests.cpp
	DeviceSettingsTests.cpp
	DeviceLabelsTests.cpp
	PatternBlockOpsTests.cpp
	JustIntonationTests.cpp
	KeyboardNotesTests.cpp
	SpatialPlacementTests.cpp
	TuningCorrectionOpsTests.cpp
	PatternSourceTests.cpp
	ArrangementOpsTests.cpp
	ArrangementTests.cpp
	PatternScrollTests.cpp
	PatternBreakTests.cpp
	CommandTests.cpp
	AzimuthSlideTests.cpp
	SendSetCommandTests.cpp
	SendLiveUpdateTests.cpp
	PerformanceTests.cpp
	SongTests.cpp
	ScaleTests.cpp
	ShutdownSignalTests.cpp
	MidiNoteInputTests.cpp
	ChordGrouperTests.cpp
	KeyRepeatFilterTests.cpp
	LaunchpadLayoutTests.cpp
	LaunchpadProtocolTests.cpp
	LaunchpadTimingTests.cpp
	ClipPlayerTests.cpp
	LivePlaybackTests.cpp
	TransportTimingTests.cpp
	PercussionTrackTests.cpp
	SF2ModulatorTests.cpp
	InstrumentResolverTests.cpp
	GeneratorOverrideTests.cpp
	EnvelopeFilterTests.cpp
	VoiceLoudnessTests.cpp
	AmbisonicEncodingTests.cpp
	AmbisonicMagLSDecoderTests.cpp
	ChorusEngineTests.cpp
	AllpassFilterTests.cpp
	SpectralEnvelopeRemapTests.cpp
	OscillatorShapingChainTests.cpp
	PadSynthPartialProfileTests.cpp
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
	StructuralSilenceTests.cpp
	BusEffectLiveUpdateTests.cpp
	HazeTests.cpp
	FractionalDelayLineTests.cpp
	TapeTransportTests.cpp
	TapeVoiceLifecycleTests.cpp
	HashFieldTests.cpp
	PlayerMultiBufferTests.cpp
	NullAudioTests.cpp
	PathsTests.cpp
	WindowsBackendTests.cpp
	WaitSetTests.cpp
	LaunchpadIOTests.cpp
	SongStructureTests.cpp
	SampleTrackTests.cpp
	TimeStretcherTests.cpp
	RecordingRingBufferTests.cpp
	MonoFifoTests.cpp
	LaunchpadPanTests.cpp
	RhythmPatternLibraryTests.cpp
	SwingTests.cpp
	TimeSignatureTests.cpp
	TempoTests.cpp
	ValueRampTests.cpp
	SinusoidBankTests.cpp
	AdditiveTests.cpp
	PadSynthTests.cpp
	InstrumentLibraryTests.cpp
	OscillatorArrayTests.cpp
	AmbisonicStackEncoderTests.cpp
)

target_link_libraries(synth_tests synth_engine)
# The terminal UI, driven on Cell UI's test terminal.
if(SYNTH_BUILD_INTERACTIVE AND SYNTH_BUILD_TERMINAL_UI)
  target_sources(synth_tests PRIVATE TerminalUITests.cpp TerminalUIViewTests.cpp)
  target_link_libraries(synth_tests synth_ui)
endif()
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

