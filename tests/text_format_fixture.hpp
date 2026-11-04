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
} // namespace cyuediff::test
