# The game simulation (docs/adr/0001-sim-data-model.md): shared by the
# client, the server, bots and tests. No rendering, no OS.
add_library(sn_sim STATIC
	include/sn/sim/protocol.h
	include/sn/sim/world.h
	src/protocol.cpp
	src/world.cpp
)
add_library(sn::sim ALIAS sn_sim)
target_include_directories(sn_sim PUBLIC include)
target_link_libraries(sn_sim PUBLIC ph::build_config ph::core)
ph_target_warnings(sn_sim)

set_property(GLOBAL APPEND PROPERTY SN_TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tests/world_test.cpp)
