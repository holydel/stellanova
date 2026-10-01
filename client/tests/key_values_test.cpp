#include "key_values.h"
#include "string_table.h"

#include <doctest/doctest.h>

TEST_CASE("client: key = value files and the strings table")
{
	const auto values = sn::ParseKeyValues("# a comment\n\n  menu.quit =  Quit \r\n"
	                                       "flight.speed = Speed: {} m/s\nbroken line\n=\n");
	REQUIRE(values.size() == 3);
	CHECK(values[0].key == "menu.quit");
	CHECK(values[0].value == "Quit");
	CHECK(values[2].key.empty());
	CHECK(sn::WriteKeyValues({{"music", "0.70"}}) == "music = 0.70\n");

	sn::StringTable strings;
	strings.Load("menu.quit = Quit\nflight.speed = Speed: {} m/s\n");
	CHECK(std::string(strings.Get("menu.quit")) == "Quit");
	CHECK(std::string(strings.Get("menu.missing")) == "menu.missing");
	CHECK(strings.Format("flight.speed", "12") == "Speed: 12 m/s");
}
