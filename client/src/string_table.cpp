#include "string_table.h"

#include "key_values.h"

#include <ph/core/log.h>

#include <algorithm>

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
} // namespace sn
