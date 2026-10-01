#pragma once

#include <string>
#include <string_view>
#include <vector>

// The text the player reads, by key, from a strings table in the pack
// (content/strings/<language>.txt): no text in the code, so that every
// language is a file.
namespace sn
{
class StringTable
{
public:
	void Load(std::string_view text);
	// The key itself when the table has no such string (logged once).
	const char* Get(const char* key);
	// Get, with "{}" replaced by `value`.
	std::string Format(const char* key, const char* value);

private:
	struct Entry
	{
		std::string key;
		std::string text;
	};
	std::vector<Entry> entries;
	std::vector<std::string> missing;
};
} // namespace sn
