# The game server (docs/adr/0007-local-server.md, 0014-accounts-on-the-game-server.md,
# 0016-sign-in-and-admin.md): the authority over matches, and the accounts'
# keeper. Local games link it into the client and talk to it over pith's
# loopback transport; online games run it as stellanova-server.
add_library(sn_server STATIC
	include/sn/server/accounts.h
	include/sn/server/server.h
	include/sn/server/signin.h
	src/accounts.cpp
	src/admin.cpp
	src/match.cpp
	src/server.cpp
)
add_library(sn::server ALIAS sn_server)
target_include_directories(sn_server PUBLIC include)
target_link_libraries(sn_server PUBLIC ph::build_config sn::sim sn::bots ph::net)
target_link_libraries(sn_server PRIVATE ph_yyjson)
ph_target_warnings(sn_server)

# Sign-ins checked with their providers (ADR 0016): HTTPS and RS256 from the
# OS's libraries, libcurl and libcrypto (the Steam Runtime sysroot's at build
# time, the machine's at run time) on Linux, WinHTTP and CNG on Windows.
# Only stellanova-server and the tests link it; elsewhere it has no HTTPS.
add_library(sn_signin STATIC
	include/sn/server/providers.h
	src/providers.cpp
)
add_library(sn::signin ALIAS sn_signin)
target_link_libraries(sn_signin PUBLIC sn::server PRIVATE ph_yyjson)
if(WIN32)
	target_sources(sn_signin PRIVATE src/https_win32.cpp)
	target_link_libraries(sn_signin PRIVATE winhttp bcrypt)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	find_package(CURL REQUIRED)
	find_package(OpenSSL REQUIRED COMPONENTS Crypto)
	target_sources(sn_signin PRIVATE src/https_linux.cpp)
	target_link_libraries(sn_signin PRIVATE CURL::libcurl OpenSSL::Crypto)
else()
	target_compile_definitions(sn_signin PRIVATE SN_NO_HTTPS=1)
endif()
ph_target_warnings(sn_signin)

# The dedicated server (docs/adr/0010-online-server.md): Linux for the hosted
# one, Windows for local tries.
if(NOT EMSCRIPTEN AND NOT ANDROID)
	add_executable(stellanova-server src/main.cpp)
	target_link_libraries(stellanova-server PRIVATE sn::server sn::signin)
	ph_target_warnings(stellanova-server)
endif()

set_property(GLOBAL APPEND PROPERTY SN_TEST_SOURCES
	${CMAKE_CURRENT_SOURCE_DIR}/tests/server_test.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/tests/hub_test.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/tests/signin_test.cpp
)
