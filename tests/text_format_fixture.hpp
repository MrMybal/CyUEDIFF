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
                                     int version = 1013, int editor = 32) {
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
        editor);
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
} // namespace cyuediff::test
