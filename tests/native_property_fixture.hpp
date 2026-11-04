#pragma once
#include "package_fixture.hpp"
namespace cyuediff::test {
inline Fixture native_property_fixture(const Bytes &payload, const std::string &structure,
                                       int version = 1013) {
    return fixture(version, 1, false, 0, {"Native", "StructProperty", structure, "AfterNative"},
                   [&](Bytes &out, const NameWriter &name) {
                       name(out, "Native");
                       name(out, "StructProperty");
                       if (version >= 1012) {
                           out.i(1);
                           name(out, structure);
                           out.i(0);
                       }
                       out.i(static_cast<int>(payload.data.size()));
                       if (version >= 1012)
                           out.put<std::uint8_t>(0);
                       else {
                           out.i(0);
                           name(out, structure);
                           out.zeros(16);
                           out.put<std::uint8_t>(0);
                       }
                       out.append(payload);
                       name(out, "AfterNative");
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
                       out.i(987);
                   });
}
inline Bytes geometry_value(const std::string &name, bool wide = true, bool changed = false) {
    const unsigned count = name.find("BoxSphereBounds") == 0 ? 7
                           : name.find("Sphere") == 0        ? 4
                           : name.find("Matrix") == 0        ? 16
                           : name.find("Plane") == 0         ? 4
                                                             : 6;
    Bytes bytes;
    for (unsigned i = 0; i < count; ++i) {
        const double value = (wide ? 1000000000000.125 : -3.5) + i + (changed && i == count - 1 ? 1 : 0);
        if (wide)
            bytes.put<double>(value);
        else
            bytes.put<float>(float(value));
    }
    if (name.find("Box") == 0 && name.find("BoxSphereBounds") != 0)
        bytes.put<std::uint8_t>(changed ? 0 : 255);
    return bytes;
}
} // namespace cyuediff::test
