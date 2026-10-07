# Bots (docs/adr/0009-bots-v0.md, 0013-ships-modules-damage.md): orders
# turned into controls, and pilots that fly ships from what the sim shows
# them. The server runs them for a match's bots and escorts; the client
# steers its own ship by orders.
add_library(sn_bots STATIC
	include/sn/bots/orders.h
	include/sn/bots/pilot.h
	src/orders.cpp
	src/pilot.cpp
)
add_library(sn::bots ALIAS sn_bots)
target_include_directories(sn_bots PUBLIC include)
target_link_libraries(sn_bots PUBLIC ph::build_config sn::sim)
ph_target_warnings(sn_bots)

set_property(GLOBAL APPEND PROPERTY SN_TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tests/pilot_test.cpp)
