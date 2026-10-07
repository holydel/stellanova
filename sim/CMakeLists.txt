# The game simulation (docs/adr/0001-sim-data-model.md): shared by the
# client, the server, bots and tests. No rendering, no OS.

# The catalog (content/catalog.json, docs/adr/0013-ships-modules-damage.md),
# built in as bytes; editing it configures again.
set(SN_CATALOG ${CMAKE_CURRENT_SOURCE_DIR}/../content/catalog.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${SN_CATALOG})
file(READ ${SN_CATALOG} SN_CATALOG_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," SN_CATALOG_BYTES "${SN_CATALOG_HEX}")
configure_file(src/catalog_data.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/catalog_data.cpp @ONLY)

add_library(sn_sim STATIC
	include/sn/sim/account.h
	include/sn/sim/catalog.h
	include/sn/sim/fitting.h
	include/sn/sim/protocol.h
	include/sn/sim/starmap.h
	include/sn/sim/world.h
	src/account.cpp
	src/catalog.cpp
	src/fitting.cpp
	src/json.h
	src/protocol.cpp
	src/starmap.cpp
	src/world.cpp
	${CMAKE_CURRENT_BINARY_DIR}/catalog_data.cpp
)
add_library(sn::sim ALIAS sn_sim)
target_include_directories(sn_sim PUBLIC include)
target_link_libraries(sn_sim PUBLIC ph::build_config ph::core PRIVATE ph_yyjson)
ph_target_warnings(sn_sim)

set_property(GLOBAL APPEND PROPERTY SN_TEST_SOURCES
	${CMAKE_CURRENT_SOURCE_DIR}/tests/world_test.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/tests/catalog_test.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/tests/account_test.cpp
)
