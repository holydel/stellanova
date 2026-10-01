# Stella Nova's tests, in one executable: each part lists its tests in
# SN_TEST_SOURCES. On the Steam Deck (cross-compiled Linux builds) they run
# over SSH, like pith's.
get_property(sources GLOBAL PROPERTY SN_TEST_SOURCES)
add_executable(sn_tests ${sources})
target_link_libraries(sn_tests PRIVATE ph::test_main sn::sim sn::server)
# The client's plain-data parts (key = value files, strings).
target_include_directories(sn_tests PRIVATE ${PROJECT_SOURCE_DIR}/client/src)
ph_target_warnings(sn_tests)

if(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
	find_program(SN_POWERSHELL NAMES pwsh powershell REQUIRED)
	add_test(NAME sn_tests
		COMMAND ${SN_POWERSHELL} -NoProfile -ExecutionPolicy Bypass
			-File ${SN_PITH_DIR}/scripts/linux-exec.ps1 $<TARGET_FILE:sn_tests>
	)
else()
	add_test(NAME sn_tests COMMAND sn_tests)
endif()
