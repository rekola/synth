add_executable(synth_tests
	test_main.cpp
	SampleDataTests.cpp
	ChannelConfigurationTests.cpp
	TunerTests.cpp
	RenderTests.cpp
	ControllerTests.cpp
	PatternBlockOpsTests.cpp
	LaunchpadLayoutTests.cpp
	LaunchpadProtocolTests.cpp
	VoiceLoudnessTests.cpp
)

target_link_libraries(synth_tests synth_engine)
target_compile_definitions(synth_tests PRIVATE
	TESTS_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
	TESTS_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}")

add_test(NAME synth_tests COMMAND synth_tests)
