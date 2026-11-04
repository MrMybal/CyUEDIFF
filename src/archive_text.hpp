#pragma once
#include "archive.hpp"
#include "text_value.hpp"
#include "string_value.hpp"
#include <cmath>
namespace cyuediff::detail {
template <class NameReader>
Json read_archive_text(Archive &a, const PackageSummary &summary, const ReadOptions &options, unsigned depth,
                       std::uint32_t &nodes, std::uint64_t &blob_bytes, NameReader name,
                       const char *type_error = "text-history", const char *value_error = "text-value",
                       const char *identity_error = "text-identity") {
    TextWireReader reader;
    reader.invariant_marker = summary.custom_versions.value("e4b068edf49442e9a231da0b2e46bb41", -1) >= 32;
    reader.always_sign = summary.custom_versions.value("e4b068edf49442e9a231da0b2e46bb41", -1) >= 21;
    reader.argument_data_enabled = true;
    reader.argument_data_variant = summary.custom_versions.value("e4b068edf49442e9a231da0b2e46bb41", -1) >= 5;
    reader.argument_data_int64 = summary.custom_versions.value("d89b5e4224bd4d468412aca8df641779", -1) >= 12;
    reader.type_error = type_error;
    reader.value_error = value_error;
    reader.identity_error = identity_error;
    reader.position = [&] { return a.tell(); };
    reader.remaining = [&] { return options.max_properties - nodes; };
    reader.check = [&](unsigned level) {
        if (level > options.max_depth)
            throw Error("limit", "Text nesting exceeds configured limit", a.tell());
        options.control.check();
        if (++nodes > options.max_properties)
            throw Error("limit", "Text work exceeds configured limit", a.tell());
    };
    reader.payload = [&](std::uint32_t bytes, unsigned level) -> Json {
        reader.check(level);
        if (bytes > options.max_string_bytes || bytes > options.max_property_bytes - blob_bytes)
            throw Error("limit", "Text generator buffers exceed configured byte limit", a.tell());
        blob_bytes += bytes;
        auto hash = a.hash(a.tell(), bytes);
        a.skip(bytes);
        return {{"bytes", bytes}, {"sha256", std::move(hash)}};
    };
    reader.scalar = [&](TextScalar kind, unsigned level) -> Json {
        reader.check(level);
        switch (kind) {
        case TextScalar::uint8:
            return a.get<std::uint8_t>();
        case TextScalar::int8:
            return a.get<std::int8_t>();
        case TextScalar::int32:
            return a.get<std::int32_t>();
        case TextScalar::uint32:
            return a.get<std::uint32_t>();
        case TextScalar::int64:
            return a.get<std::int64_t>();
        case TextScalar::uint64:
            return a.get<std::uint64_t>();
        case TextScalar::float32: {
            const auto v = a.get<float>();
            if (!std::isfinite(v))
                throw Error(value_error, "Non-finite text argument", a.tell());
            return v;
        }
        case TextScalar::float64: {
            const auto v = a.get<double>();
            if (!std::isfinite(v))
                throw Error(value_error, "Non-finite text argument", a.tell());
            return v;
        }
        case TextScalar::string: {
            const auto offset = a.tell();
            auto text = a.string();
            validate_byte_string(text, false, options.control, value_error, offset);
            return text;
        }
        case TextScalar::name:
            return name();
        }
        throw Error(value_error, "Unknown text primitive", a.tell());
    };
    return reader.text(depth);
}
} // namespace cyuediff::detail
