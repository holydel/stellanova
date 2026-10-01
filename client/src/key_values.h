#pragma once

#include <ph/core/types.h>

#include <string>
#include <string_view>
#include <vector>

// Text files of "key = value" lines (UTF-8): the strings tables and the
// settings. Blank lines and lines starting with # are skipped; spaces around
// keys and values are trimmed.
namespace sn
{
struct KeyValue
{
	std::string key;
	std::string value;
};

std::vector<KeyValue> ParseKeyValues(std::string_view text);
std::string WriteKeyValues(const std::vector<KeyValue>& values);
} // namespace sn
