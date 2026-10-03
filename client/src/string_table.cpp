#include "string_table.h"

#include "key_values.h"

#include <ph/core/log.h>

#include <algorithm>
#include <cstring>

namespace sn
{
void StringTable::Load(std::string_view text)
{
	entries.clear();
	for (KeyValue& value : ParseKeyValues(text))
		entries.push_back({std::move(value.key), std::move(value.value)});
}

const char* StringTable::Get(const char* key)
{
	for (const Entry& entry : entries)
	{
		if (entry.key == key)
			return entry.text.c_str();
	}
	if (std::find(missing.begin(), missing.end(), key) == missing.end())
	{
		missing.emplace_back(key);
		PH_LOG_WARN("strings: no text for %s", key);
	}
	return key;
}

std::string StringTable::Format(const char* key, const char* value)
{
	std::string text = Get(key);
	const ph::usize at = text.find("{}");
	if (at != std::string::npos)
		text.replace(at, 2, value);
	return text;
}

std::string StringTable::Format(const char* key, const char* first, const char* second)
{
	std::string text = Get(key);
	ph::usize at = text.find("{}");
	if (at == std::string::npos)
		return text;
	text.replace(at, 2, first);
	// After what went in: a "{}" in `first` stays as it is.
	at = text.find("{}", at + std::strlen(first));
	if (at != std::string::npos)
		text.replace(at, 2, second);
	return text;
}
} // namespace sn
