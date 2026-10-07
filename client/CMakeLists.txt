# The game client, `stellanova`, on the backend each OS ships with (Vulkan;
# WebGPU on the web), with stellanova.pak (content/initial.json).
ph_add_graphics_app(stellanova BACKEND ${PH_DEFAULT_BACKEND}
	SOURCES
		src/colony_view.cpp
		src/colony_view.h
		src/connection.cpp
		src/connection.h
		src/fitting_screen.cpp
		src/fitting_screen.h
		src/flight.cpp
		src/flight.h
		src/flight_effects.cpp
		src/flight_effects.h
		src/hub.cpp
		src/hub.h
		src/input_kind.h
		src/inventory.cpp
		src/inventory.h
		src/inventory_items.cpp
		src/inventory_items.h
		src/key_values.cpp
		src/key_values.h
		src/main.cpp
		src/menu.cpp
		src/menu.h
		src/prediction.cpp
		src/prediction.h
		src/resources.cpp
		src/resources.h
		src/settings.cpp
		src/settings.h
		src/ship_view.cpp
		src/ship_view.h
		src/string_table.cpp
		src/string_table.h
		src/ui.cpp
		src/ui.h
	LIBS sn::sim sn::bots sn::server ph::audio
	PACK ${PROJECT_SOURCE_DIR}/content/initial.json
	PACK_NAME stellanova.pak
	DISPLAY_NAME "Stella Nova"
)
if(TARGET stellanova)
	target_compile_definitions(stellanova PRIVATE SN_VERSION="${PROJECT_VERSION}")
	# The menu's splash and the colony's buildings (content/art.json):
	# generated files kept on this PC only for now (docs/content-generation.md),
	# in a pack of its own that the game reads with its main pack. Builds
	# without the splash show the menu on the dark and Kenney's buildings. The
	# web preloads every pack before starting, so there the splash goes alone,
	# without mip levels (content/art-web.json: 8 MB more to load, the
	# developer's choice of 2026-10-04). Android's APK gets it from
	# scripts/build.ps1 (ph_add_pack leaves Android's packs to it).
	set(art_pack 0)
	if(EXISTS ${PROJECT_SOURCE_DIR}/content/generated/splash/menu.png)
		set(art_manifest ${PROJECT_SOURCE_DIR}/content/art.json)
		if(EMSCRIPTEN)
			set(art_manifest ${PROJECT_SOURCE_DIR}/content/art-web.json)
		endif()
		ph_add_pack(stellanova MANIFEST ${art_manifest} NAME stellanova-art.pak)
		set(art_pack 1)
	endif()
	target_compile_definitions(stellanova PRIVATE SN_ART_PACK=${art_pack})
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
	${CMAKE_CURRENT_SOURCE_DIR}/tests/inventory_items_test.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/tests/key_values_test.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/tests/prediction_test.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/inventory_items.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/key_values.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/prediction.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/string_table.cpp)
