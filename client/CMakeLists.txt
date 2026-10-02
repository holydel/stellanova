# The game client, `stellanova`, on the backend each OS ships with (Vulkan;
# WebGPU on the web), with stellanova.pak (content/initial.json).
ph_add_graphics_app(stellanova BACKEND ${PH_DEFAULT_BACKEND}
	SOURCES
		src/flight.cpp
		src/flight.h
		src/flight_effects.cpp
		src/flight_effects.h
		src/key_values.cpp
		src/key_values.h
		src/main.cpp
		src/menu.cpp
		src/menu.h
		src/resources.cpp
		src/resources.h
		src/settings.cpp
		src/settings.h
		src/string_table.cpp
		src/string_table.h
		src/ui.cpp
		src/ui.h
	LIBS sn::sim sn::server ph::audio
	PACK ${PROJECT_SOURCE_DIR}/content/initial.json
	PACK_NAME stellanova.pak
	DISPLAY_NAME "Stella Nova"
)
if(TARGET stellanova)
	target_compile_definitions(stellanova PRIVATE SN_VERSION="${PROJECT_VERSION}")
	if(SN_BUILD_TESTS AND PH_CAN_RUN_APPS)
		add_test(NAME stellanova_menu COMMAND stellanova --frames 30 --hidden --platform none --mute)
		add_test(NAME stellanova_flight
			COMMAND stellanova --frames 30 --hidden --platform none --mute --flight)
		# Vulkan's validation with its synchronization checks (pith ADR 0034):
		# any error it logs fails the smoke runs.
		if(PH_DEFAULT_BACKEND STREQUAL "vulkan")
			set_tests_properties(stellanova_menu stellanova_flight PROPERTIES
				ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1"
				FAIL_REGULAR_EXPRESSION " E vulkan: ")
		endif()
		# The menu against a reference picture (1280x720 at 100% scale), like
		# pith's samples: copy a new screenshot over it when a change is meant.
		ph_pith_tool(pith stellanova)
		add_test(NAME stellanova_screenshot
			COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:stellanova> -DPITH=${pith}
				-DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/screenshot.png
				-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/stellanova.png
				-P ${SN_PITH_DIR}/cmake/ph_screenshot_test.cmake)
	endif()
endif()

set_property(GLOBAL APPEND PROPERTY SN_TEST_SOURCES
	${CMAKE_CURRENT_SOURCE_DIR}/tests/key_values_test.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/key_values.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/string_table.cpp)
