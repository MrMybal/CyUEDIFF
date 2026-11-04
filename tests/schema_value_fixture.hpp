#pragma once
#include "schema_collection_fixture.hpp"
namespace cyuediff::test {
inline Bytes text_value(const std::string &source = "Original source") {
    Bytes bytes;
    bytes.put<std::uint32_t>(0x40000000);
    bytes.put<std::int8_t>(0);
    bytes.string("Stored Namespace");
    bytes.string("StableKey");
    bytes.string(source);
    return bytes;
}
inline Bytes invariant_text_value(bool present = true) {
    Bytes bytes;
    bytes.i(2);
    bytes.put<std::int8_t>(-1);
    bytes.i(present ? 1 : 0);
    if (present)
        bytes.string("Invariant source");
    return bytes;
}
inline Bytes table_text_value() {
    Bytes bytes;
    bytes.i(0);
    bytes.put<std::int8_t>(11);
    collection_name(bytes, 2);
    bytes.string("EntryKey");
    return bytes;
}
} // namespace cyuediff::test
