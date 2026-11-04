#pragma once
#include "schema_collection_fixture.hpp"
namespace cyuediff::test {
inline Bytes optional_value(const Bytes &inner = {}, bool present = true) {
    Bytes value;
    value.i(present ? 1 : 0);
    if (present)
        value.append(inner);
    return value;
}
inline Bytes field_path_value(const std::vector<int> &names = {3, 2}, int owner = 1) {
    Bytes value;
    value.i(static_cast<int>(names.size()));
    for (int name : names) {
        value.i(name);
        value.i(0);
    }
    value.i(owner);
    return value;
}
} // namespace cyuediff::test
