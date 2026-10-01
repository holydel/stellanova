# The game server (docs/adr/0007-local-server.md): the authority over a
# match. Local games link it into the client and talk to it over pith's
# loopback transport; online games will run it as stellanova-server.
add_library(sn_server STATIC
	include/sn/server/server.h
	src/server.cpp
)
add_library(sn::server ALIAS sn_server)
target_include_directories(sn_server PUBLIC include)
target_link_libraries(sn_server PUBLIC ph::build_config sn::sim ph::net)
ph_target_warnings(sn_server)

set_property(GLOBAL APPEND PROPERTY SN_TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tests/server_test.cpp)
