#pragma once
#include "package_fixture.hpp"
namespace cyuediff::test {
struct ReflectionField {
    std::string type = "IntProperty", name = "Value";
    std::vector<ReflectionField> nested;
    std::string metadata = "Visible value";
    int dimension = 1, size = 4, reference = -1;
    std::uint64_t flags = 0x1000000000000001ull;
    bool native_bool = true;
};
inline Fixture reflection_fixture(const std::vector<ReflectionField> &fields = {{}}, bool cooked = false,
                                  int version = 1013, int core = 4, int framework = 37, bool guid = false,
                                  bool reorder_names = false) {
    std::vector<std::string> names = {"DisplayName", "Notify", "Property"};
    std::function<void(const ReflectionField &)> collect = [&](const ReflectionField &f) {
        names.push_back(f.type);
        names.push_back(f.name);
        for (const auto &inner : f.nested)
            collect(inner);
    };
    for (const auto &f : fields)
        collect(f);
    Bytes native;
    auto f = fixture(
        version, 1, reorder_names, 0, names,
        [&](Bytes &, const NameWriter &name) {
            native.i(guid ? 1 : 0);
            if (guid)
                for (unsigned word : {1u, 2u, 3u, 4u})
                    native.put(word);
            native.i(-1); // A valid imported superclass identity.
            native.i(1);
            native.i(1); // One referenced child UField.
            native.i(static_cast<int>(fields.size()));
            std::function<void(const ReflectionField &)> write = [&](const ReflectionField &field) {
                name(native, field.type);
                if (field.type == "None")
                    return;
                name(native, field.name);
                native.put<std::uint32_t>(0x40);
                if (!cooked) {
                    native.i(field.metadata.empty() ? 0 : 1);
                    if (!field.metadata.empty()) {
                        native.i(1);
                        name(native, "DisplayName");
                        native.string(field.metadata);
                    }
                }
                native.i(field.dimension);
                native.i(field.size);
                native.put(field.flags);
                native.put<std::uint16_t>(7);
                name(native, "Notify");
                native.put<std::uint8_t>(3);
                const auto &t = field.type;
                if (t == "ObjectProperty" || t == "WeakObjectProperty" || t == "LazyObjectProperty" ||
                    t == "SoftObjectProperty" || t == "ClassProperty" || t == "SoftClassProperty") {
                    native.i(field.reference);
                    if (t == "ClassProperty" || t == "SoftClassProperty")
                        native.i(1);
                } else if (t == "StructProperty" || t == "ByteProperty" || t == "InterfaceProperty" ||
                           t.find("DelegateProperty") != std::string::npos)
                    native.i(field.reference);
                else if (t == "EnumProperty") {
                    native.i(field.reference);
                    write(field.nested.empty() ? ReflectionField{"None"} : field.nested[0]);
                } else if (t == "ArrayProperty" || t == "SetProperty" || t == "OptionalProperty" ||
                           t == "MapProperty") {
                    write(field.nested.empty() ? ReflectionField{"None"} : field.nested[0]);
                    if (t == "MapProperty")
                        write(field.nested.size() < 2 ? ReflectionField{"None"} : field.nested[1]);
                } else if (t == "BoolProperty") {
                    for (unsigned byte : {1u, 0u, 1u, 255u, 1u, field.native_bool ? 1u : 0u})
                        native.put<std::uint8_t>(static_cast<std::uint8_t>(byte));
                } else if (t == "FieldPathProperty")
                    name(native, "Property");
            };
            for (const auto &field : fields)
                write(field);
        },
        -1, -1, core, -1, -1, framework, -1, "BlueprintGeneratedClass", "/Script/Engine");
    f.script.data.resize(f.script.data.size() - 4);
    f.script.append(native);
    f.script.put<std::uint32_t>(0xdeadbeef); // Unread script/class tail remains fingerprinted.
    const auto size = std::int64_t(f.script.data.size());
    std::memcpy(f.header.data.data() + f.export_offset + 28, &size, 8);
    if (cooked) {
        const std::string marker = "/Game/TestAsset";
        auto found = std::search(f.header.data.begin(), f.header.data.end(), marker.begin(), marker.end());
        f.header.patch(std::size_t(found - f.header.data.begin()) + marker.size() + 1, 0x200);
    }
    return f;
}
} // namespace cyuediff::test
