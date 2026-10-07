#pragma once

#include <ph/core/types.h>

#include <yyjson.h>

#include <cstdlib>
#include <string>
#include <string_view>

// Small helpers over yyjson for the sim's JSON: the catalog and accounts.
namespace sn::sim::json
{
using ph::usize;

inline yyjson_val* Get(yyjson_val* object, const char* key)
{
	return yyjson_is_obj(object) ? yyjson_obj_get(object, key) : nullptr;
}

inline double Number(yyjson_val* object, const char* key, double fallback = 0.0)
{
	yyjson_val* value = Get(object, key);
	return yyjson_is_num(value) ? yyjson_get_num(value) : fallback;
}

inline float Float(yyjson_val* object, const char* key, float fallback = 0.0f)
{
	return float(Number(object, key, fallback));
}

inline ph::u32 Count(yyjson_val* object, const char* key, ph::u32 fallback = 0)
{
	const double value = Number(object, key, fallback);
	return value > 0.0 && value < 4294967295.0 ? ph::u32(value) : 0;
}

inline std::string_view Text(yyjson_val* value)
{
	return yyjson_is_str(value) ? std::string_view(yyjson_get_str(value), yyjson_get_len(value))
	                            : std::string_view();
}

inline std::string_view Text(yyjson_val* object, const char* key) { return Text(Get(object, key)); }

inline bool Flag(yyjson_val* object, const char* key, bool fallback = false)
{
	yyjson_val* value = Get(object, key);
	return yyjson_is_bool(value) ? yyjson_get_bool(value) : fallback;
}

// A document freed when it goes out of scope.
class Document
{
public:
	explicit Document(std::string_view text)
		: doc(yyjson_read(text.data(), text.size(), YYJSON_READ_ALLOW_TRAILING_COMMAS))
	{
	}
	~Document() { yyjson_doc_free(doc); }
	Document(const Document&) = delete;
	Document& operator=(const Document&) = delete;

	yyjson_val* Root() const { return doc ? yyjson_doc_get_root(doc) : nullptr; }

private:
	yyjson_doc* doc;
};

// A document to write, freed when it goes out of scope.
class Builder
{
public:
	Builder() : doc(yyjson_mut_doc_new(nullptr)) {}
	~Builder() { yyjson_mut_doc_free(doc); }
	Builder(const Builder&) = delete;
	Builder& operator=(const Builder&) = delete;

	yyjson_mut_doc* Doc() const { return doc; }
	yyjson_mut_val* Object() const { return yyjson_mut_obj(doc); }
	yyjson_mut_val* Array() const { return yyjson_mut_arr(doc); }

	void Set(yyjson_mut_val* object, const char* key, double value) const
	{
		yyjson_mut_obj_add_real(doc, object, key, value);
	}
	void SetInt(yyjson_mut_val* object, const char* key, ph::i64 value) const
	{
		yyjson_mut_obj_add_int(doc, object, key, value);
	}
	void Set(yyjson_mut_val* object, const char* key, bool value) const
	{
		yyjson_mut_obj_add_bool(doc, object, key, value);
	}
	// Copies the text.
	void Set(yyjson_mut_val* object, const char* key, std::string_view value) const
	{
		yyjson_mut_obj_add_strncpy(doc, object, key, value.data(), value.size());
	}
	void Set(yyjson_mut_val* object, const char* key, yyjson_mut_val* value) const
	{
		yyjson_mut_obj_add_val(doc, object, key, value);
	}
	// Copies the key.
	void SetNumberAt(yyjson_mut_val* object, std::string_view key, double value) const
	{
		yyjson_mut_obj_add(object, yyjson_mut_strncpy(doc, key.data(), key.size()),
		                   yyjson_mut_real(doc, value));
	}
	void Append(yyjson_mut_val* array, yyjson_mut_val* value) const
	{
		yyjson_mut_arr_append(array, value);
	}
	void AppendText(yyjson_mut_val* array, std::string_view value) const
	{
		yyjson_mut_arr_append(array, yyjson_mut_strncpy(doc, value.data(), value.size()));
	}
	void AppendNumber(yyjson_mut_val* array, double value) const
	{
		yyjson_mut_arr_append(array, yyjson_mut_real(doc, value));
	}

	std::string Write(yyjson_mut_val* root, bool pretty = false) const
	{
		yyjson_mut_doc_set_root(doc, root);
		usize length = 0;
		char* text = yyjson_mut_write(doc, pretty ? YYJSON_WRITE_PRETTY_TWO_SPACES : 0, &length);
		std::string out = text ? std::string(text, length) : std::string();
		free(text);
		return out;
	}

private:
	yyjson_mut_doc* doc;
};
} // namespace sn::sim::json
