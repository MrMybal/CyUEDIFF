#pragma once
#include "package_fixture.hpp"
namespace cyuediff::test {
inline Fixture native_property_fixture(const Bytes &payload, const std::string &structure, int version = 1013,
                                       const std::function<Bytes(const NameWriter &)> &writer = {}) {
    return fixture(version, 1, false, 0,
                   {"Native", "StructProperty", structure, "AfterNative", "Origin", "BoxExtent",
                    "SphereRadius", "Center", "W", "Vector", "Vector3f", "Vector3d", "FloatProperty",
                    "DoubleProperty"},
                   [&](Bytes &out, const NameWriter &name) {
                       const auto actual = writer ? writer(name) : payload;
                       name(out, "Native");
                       name(out, "StructProperty");
                       if (version >= 1012) {
                           out.i(1);
                           name(out, structure);
                           out.i(0);
                       }
                       out.i(static_cast<int>(actual.data.size()));
                       if (version >= 1012)
                           out.put<std::uint8_t>(0);
                       else {
                           out.i(0);
                           name(out, structure);
                           out.zeros(16);
                           out.put<std::uint8_t>(0);
                       }
                       out.append(actual);
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
inline Fixture reflected_geometry_fixture(const std::string &structure, int version = 1013,
                                          bool changed = false) {
    const bool wide = version >= 1004 && structure.back() != 'f';
    return native_property_fixture({}, structure, version, [&](const NameWriter &name) {
        Bytes body;
        auto field = [&](const char *key, const char *type, const char *inner, const Bytes &payload) {
            name(body, key);
            name(body, type);
            if (version >= 1012) {
                body.i(inner ? 1 : 0);
                if (inner) {
                    name(body, inner);
                    body.i(0);
                }
            }
            body.i(static_cast<int>(payload.data.size()));
            if (version >= 1012)
                body.put<std::uint8_t>(0);
            else {
                body.i(0);
                if (inner) {
                    name(body, inner);
                    body.zeros(16);
                }
                body.put<std::uint8_t>(0);
            }
            body.append(payload);
        };
        auto vector = [&](const char *key, int offset) {
            Bytes payload;
            for (unsigned i = 0; i < 3; ++i) {
                const double v = (wide ? 1000000000000.125 : -3.5) + offset + i;
                if (wide)
                    payload.put<double>(v);
                else
                    payload.put<float>(float(v));
            }
            field(key, "StructProperty",
                  structure.back() == 'f'   ? "Vector3f"
                  : structure.back() == 'd' ? "Vector3d"
                                            : "Vector",
                  payload);
        };
        const bool sphere = structure.find("Sphere") == 0;
        vector(sphere ? "Center" : "Origin", 0);
        if (!sphere)
            vector("BoxExtent", 3);
        Bytes radius;
        const double value = (wide ? 1000000000000.125 : -3.5) + (sphere ? 3 : 6) + (changed ? 1 : 0);
        if (wide)
            radius.put<double>(value);
        else
            radius.put<float>(float(value));
        field(sphere ? "W" : "SphereRadius", wide ? "DoubleProperty" : "FloatProperty", nullptr, radius);
        name(body, "None");
        return body;
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
