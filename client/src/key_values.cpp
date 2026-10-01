#include "key_values.h"

namespace sn
{
namespace
{
std::string_view Trim(std::string_view text)
{
	const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
	while (!text.empty() && space(text.front()))
		text.remove_prefix(1);
	while (!text.empty() && space(text.back()))
		text.remove_suffix(1);
	return text;
}
} // namespace

std::vector<KeyValue> ParseKeyValues(std::string_view text)
{
	std::vector<KeyValue> values;
	while (!text.empty())
	{
		const ph::usize end = text.find('\n');
		std::string_view line = Trim(text.substr(0, end));
		text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
		const ph::usize equals = line.find('=');
		if (line.empty() || line.front() == '#' || equals == std::string_view::npos)
			continue;
		values.push_back({std::string(Trim(line.substr(0, equals))),
		                  std::string(Trim(line.substr(equals + 1)))});
	}
	return values;
}

std::string WriteKeyValues(const std::vector<KeyValue>& values)
{
	std::string text;
	for (const KeyValue& value : values)
		text += value.key + " = " + value.value + "\n";
	return text;
}
} // namespace sn
