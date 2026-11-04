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
} // namespace cyuediff::test
