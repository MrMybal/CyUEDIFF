#pragma once
#include "schema_value_fixture.hpp"
namespace cyuediff::test {
inline Bytes byte_string_value(const std::string &value) {
    Bytes bytes;
    bytes.i(static_cast<int>(value.size()));
    bytes.data.insert(bytes.data.end(), value.begin(), value.end());
    return bytes;
}
inline Fixture text_property_fixture(const Bytes &value, const std::string &type = "TextProperty",
                                     int version = 1013, int editor = 32, int release = -1) {
    return fixture(
        version, 1, false, 0, {"Formatted", type, "AfterFormat"},
        [&](Bytes &out, const NameWriter &name) {
            name(out, "Formatted");
            name(out, type);
            if (version >= 1012)
                out.i(0);
            out.i(static_cast<int>(value.data.size()));
            if (version >= 1012)
                out.put<std::uint8_t>(0);
            else {
                out.i(0);
                out.put<std::uint8_t>(0);
            }
            out.append(value);
            name(out, "AfterFormat");
            name(out, "IntProperty");
            if (version >= 1012)
                out.i(0);
            out.i(4);
            if (version >= 1012)
                out.put<std::uint8_t>(0);
            else {
                out.i(0);
                out.put<std::uint8_t>(0);
            }
            out.i(876);
        },
        editor, release);
}
inline Bytes transform_text_value(const Bytes &source = text_value(), unsigned operation = 0) {
    Bytes value;
    value.i(0);
    value.put<std::int8_t>(10);
    value.append(source);
    value.put<std::uint8_t>(static_cast<std::uint8_t>(operation));
    return value;
}
inline Bytes text_argument(unsigned kind, const Bytes &payload) {
    Bytes value;
    value.put<std::int8_t>(static_cast<std::int8_t>(kind));
    value.append(payload);
    return value;
}
inline Bytes ordered_text_value(const std::vector<Bytes> &arguments,
                                const Bytes &format = text_value("{0}")) {
    Bytes value;
    value.i(0);
    value.put<std::int8_t>(2);
    value.append(format);
    value.i(static_cast<int>(arguments.size()));
    for (const auto &argument : arguments)
        value.append(argument);
    return value;
}
inline Bytes named_text_value(const std::vector<std::pair<std::string, Bytes>> &arguments,
                              const Bytes &format = text_value("{Count}: {Label}")) {
    Bytes value;
    value.i(0);
    value.put<std::int8_t>(1);
    value.append(format);
    value.i(static_cast<int>(arguments.size()));
    for (const auto &argument : arguments) {
        value.string(argument.first);
        value.append(argument.second);
    }
    return value;
}
inline Bytes number_format_options(bool modern = true, int rounding = 0) {
    Bytes options;
    if (modern)
        options.i(1);
    options.i(0);
    options.put<std::int8_t>(static_cast<std::int8_t>(rounding));
    for (int value : {1, 324, 2, 4})
        options.i(value);
    return options;
}
inline Bytes numeric_text_value(unsigned history = 4, bool with_options = true,
                                const std::string &culture = "fr-FR", bool modern = true, int rounding = 0,
                                double number = 1000000000000.125) {
    Bytes value;
    value.i(0);
    value.put<std::int8_t>(static_cast<std::int8_t>(history));
    if (history == 6)
        value.string("EUR");
    Bytes source;
    source.put<double>(number);
    value.append(text_argument(3, source));
    value.i(with_options ? 1 : 0);
    if (with_options)
        value.append(number_format_options(modern, rounding));
    value.string(culture);
    return value;
}
inline Bytes calendar_text_value(unsigned history = 9, std::int64_t ticks = 638980123456789012LL,
                                 int dateStyle = 2, int timeStyle = 1,
                                 const std::string &zone = "Europe/Paris",
                                 const std::string &culture = "fr-FR",
                                 const std::string &pattern = "yyyy-MM-dd HH:mm:ss") {
    Bytes value;
    value.i(0);
    value.put<std::int8_t>(static_cast<std::int8_t>(history));
    value.put<std::int64_t>(ticks);
    if (history != 8)
        value.put<std::int8_t>(static_cast<std::int8_t>(dateStyle));
    if (history != 7)
        value.put<std::int8_t>(static_cast<std::int8_t>(timeStyle));
    if (history == 9 && dateStyle == 5)
        value.string(pattern);
    value.string(zone);
    value.string(culture);
    return value;
}
inline Bytes data_text_value(const std::vector<std::pair<std::string, Bytes>> &arguments,
                             const Bytes &format = text_value("{Count}: {Label}")) {
    Bytes value;
    value.i(0);
    value.put<std::int8_t>(3);
    value.append(format);
    value.i(static_cast<int>(arguments.size()));
    for (const auto &argument : arguments) {
        value.string(argument.first);
        value.append(argument.second);
    }
    return value;
}
inline Bytes generator_text_value(int nameIndex = 2,
                                  const std::vector<unsigned char> &contents = {0, 1, 255, 7},
                                  int suffix = 0) {
    Bytes value;
    value.i(0);
    value.put<std::int8_t>(12);
    value.i(nameIndex);
    value.i(suffix);
    if (nameIndex != 0 || suffix != 0) {
        value.i(static_cast<int>(contents.size()));
        value.data.insert(value.data.end(), contents.begin(), contents.end());
    }
    return value;
}
inline Bytes format_text_payload(const std::string &mode) {
    const bool changed = mode.find("changed-") == 0;
    const auto type = changed ? mode.substr(8) : mode;
    Bytes integer;
    integer.put<std::int64_t>(changed ? 42 : 41);
    auto count = text_argument(0, integer);
    auto label = text_argument(
        4, transform_text_value(text_value(changed ? "Edited label" : "Original label"), changed ? 1 : 0));
    if (type == "generator" || type == "empty-generator" || type == "none-generator" ||
        type == "bad-generator-size" || type == "truncated-generator") {
        auto value = generator_text_value(
            type == "none-generator" ? 0 : 2,
            type == "empty-generator"
                ? std::vector<unsigned char>{}
                : std::vector<unsigned char>{0, 1, 255, static_cast<unsigned char>(changed ? 6 : 7)});
        if (type == "bad-generator-size")
            value.patch(13, -1);
        if (type == "truncated-generator")
            value.data.pop_back();
        return value;
    }
    if (type == "date" || type == "time" || type == "date-time" || type == "custom-date-time" ||
        type == "bad-style")
        return calendar_text_value(type == "date"   ? 7
                                   : type == "time" ? 8
                                                    : 9,
                                   changed ? 638980123456789013LL : 638980123456789012LL,
                                   type == "bad-style"          ? 6
                                   : type == "custom-date-time" ? 5
                                   : changed                    ? 4
                                                                : 2,
                                   changed ? 4 : 1, changed ? "UTC" : "Europe/Paris",
                                   changed ? "en-US" : "fr-FR", changed ? "yyyy" : "yyyy-MM-dd");
    if (type == "data")
        return data_text_value({{"Count", count},
                                {"Count", text_argument(5, Bytes{{2}})},
                                {"Label", text_argument(4, calendar_text_value(9, changed ? 7 : 6))}});
    if (type == "empty-data")
        return data_text_value({});
    if (type == "bad-data")
        return data_text_value({{"Bad", text_argument(1, {})}});
    if (type == "transform")
        return transform_text_value(text_value(changed ? "Edited label" : "Original label"), changed ? 1 : 0);
    if (type == "number" || type == "currency" || type == "percent" || type == "bad-rounding") {
        auto value = numeric_text_value(type == "currency"  ? 6
                                        : type == "percent" ? 5
                                                            : 4,
                                        true, changed ? "en-US" : "fr-FR", true,
                                        type == "bad-rounding" ? 7
                                        : changed              ? 2
                                                               : 0,
                                        changed ? 1000000000001.125 : 1000000000000.125);
        if (type == "currency" && changed) {
            value.data[9] = 'G';
            value.data[10] = 'B';
            value.data[11] = 'P';
        }
        return value;
    }
    if (type == "ordered")
        return ordered_text_value({count, label});
    if (type == "bad-argument")
        return ordered_text_value({text_argument(99, {})});
    if (type == "empty-ordered")
        return ordered_text_value({});
    if (type == "empty-named")
        return named_text_value({});
    if (type == "duplicate")
        return named_text_value({{"Count", count}, {"Count", count}});
    if (type == "unsupported") {
        Bytes value;
        value.i(0);
        value.put<std::int8_t>(42);
        return value;
    }
    const auto format = text_value(changed ? "{Label}: {Count}" : "{Count}: {Label}");
    return type == "reordered-named" ? named_text_value({{"Label", label}, {"Count", count}}, format)
                                     : named_text_value({{"Count", count}, {"Label", label}}, format);
}
inline std::string format_utf8_source(bool changed = false) {
    return changed ? "Caf\xc3\xa9 \xf0\x9f\x98\x81" : "Caf\xc3\xa9 \xf0\x9f\x98\x80";
}
inline void write_text_format_fixtures(const fs::path &root) {
    fs::create_directories(root);
    auto mapping = collection_schema_fixture(collection_type(11), collection_type(29), collection_type(30));
    mapping.save(root / "formats.usmap");
    for (const char *name : {"named",
                             "changed-named",
                             "reordered-named",
                             "ordered",
                             "changed-ordered",
                             "transform",
                             "changed-transform",
                             "number",
                             "changed-number",
                             "percent",
                             "currency",
                             "changed-currency",
                             "empty-named",
                             "empty-ordered",
                             "duplicate",
                             "bad-argument",
                             "bad-rounding",
                             "unsupported",
                             "bad-utf8",
                             "truncated",
                             "zero",
                             "date",
                             "changed-date",
                             "time",
                             "changed-time",
                             "date-time",
                             "changed-date-time",
                             "custom-date-time",
                             "changed-custom-date-time",
                             "bad-style",
                             "data",
                             "changed-data",
                             "empty-data",
                             "bad-data",
                             "generator",
                             "changed-generator",
                             "empty-generator",
                             "none-generator",
                             "bad-generator-size",
                             "truncated-generator"}) {
        const std::string mode = name;
        Bytes values;
        if (mode == "zero") {
            values.put<std::uint16_t>(0x0780);
            values.put<std::uint8_t>(7);
        } else {
            values.put<std::uint16_t>(0x0700);
            values.append(format_text_payload(mode));
            if (mode != "truncated-generator") {
                values.append(byte_string_value(mode == "bad-utf8"
                                                    ? std::string("\xc0\x80")
                                                    : format_utf8_source(mode.find("changed-") == 0)));
                values.append(
                    byte_string_value(mode.find("changed-") == 0 ? "Edited ASCII" : "Stored ASCII"));
            }
            if (mode == "truncated")
                values.data.pop_back();
            save(root, mode, text_property_fixture(format_text_payload(mode), "TextProperty", 1013, 32, 12));
        }
        auto package = collection_zen_fixture(values, true);
        package.save(root / (mode + ".ucas"));
        zen_toc(package).save(root / (mode + ".utoc"));
    }
    save(root, "utf8", text_property_fixture(byte_string_value(format_utf8_source()), "Utf8StrProperty"));
    save(root, "changed-utf8",
         text_property_fixture(byte_string_value(format_utf8_source(true)), "Utf8StrProperty"));
    save(root, "bad-utf8", text_property_fixture(byte_string_value("\xc0\x80"), "Utf8StrProperty"));
    save(root, "ascii", text_property_fixture(byte_string_value("Stored ASCII"), "AnsiStrProperty"));
    auto global = script_fixture();
    global.save(root / "global.ucas");
    zen_toc(global, 5).save(root / "global.utoc");
    Json manifest = {{"format", "cyuediff.zen-schema/2"},
                     {"profile", "ue5.5"},
                     {"usmap", "formats.usmap"},
                     {"sha256", sha256_bytes(mapping.data)},
                     {"bindings", {{"/Script/Test.DemoAsset", {{"schema", "Collections"}, {"offset", 0}}}}},
                     {"nativeStructs", Json::object()}};
    std::ofstream file(root / "formats.json", std::ios::binary);
    file << manifest.dump(2) << '\n';
    if (!file)
        throw std::runtime_error("Cannot write text format context fixture");
}
} // namespace cyuediff::test
