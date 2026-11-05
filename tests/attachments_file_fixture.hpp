#pragma once
#include "attachments_model.hpp"
#include "package_fixture.hpp"
namespace cyuediff::test {
// Independent tagged properties, object tables and raw-reference wire arrays.
inline Fixture attachments_file_fixture(const Package &model = virtual_bones_model(), int version = 1013) {
    std::vector<std::string> names = {"Skeleton",
                                      "SkeletalMeshSocket",
                                      "/Script/Engine",
                                      "Rig",
                                      "Grip",
                                      "Muzzle",
                                      "NameProperty",
                                      "ObjectProperty",
                                      "Vector",
                                      "Rotator",
                                      "VirtualBone",
                                      "root",
                                      "arm",
                                      "tip"};
    auto collect = [&](const auto &self, const Json &v) -> void {
        if (v.is_object())
            for (auto it = v.begin(); it != v.end(); ++it) {
                names.push_back(it.key());
                self(self, *it);
            }
        else if (v.is_array())
            for (const auto &x : v)
                self(self, x);
        else if (v.is_string())
            names.push_back(v.get<std::string>());
    };
    for (const auto &e : model.exports) {
        collect(collect, e.properties);
        collect(collect, e.native_data);
    }
    std::vector<Bytes> scripts(model.exports.size()), object_names(model.exports.size());
    Bytes owner_class, socket_class, engine, native;
    auto f = fixture(
        version, 1, false, 0, names,
        [&](Bytes &root, const NameWriter &name) {
            name(owner_class, "Skeleton");
            name(socket_class, "SkeletalMeshSocket");
            name(engine, "/Script/Engine");
            auto tag = [&](Bytes &target, const std::string &field, const Json &type, const Bytes &payload,
                           bool boolean = false) {
                const auto type_name = type.at("name").get<std::string>();
                name(target, field);
                auto descriptor = [&](const auto &self, const Json &t) -> void {
                    name(target, t.at("name").get<std::string>());
                    const auto params = t.value("parameters", Json::array());
                    target.i(int(params.size()));
                    for (const auto &p : params)
                        self(self, p);
                };
                if (version >= 1012)
                    descriptor(descriptor, type);
                else
                    name(target, type_name);
                target.i(type_name == "BoolProperty" ? 0 : int(payload.data.size()));
                if (version >= 1012)
                    target.put<std::uint8_t>(boolean ? 0x10 : 0);
                else {
                    target.i(0);
                    if (type_name == "BoolProperty")
                        target.put<std::uint8_t>(boolean);
                    if (type_name == "StructProperty") {
                        name(target, type.at("parameters")[0].at("name").get<std::string>());
                        target.zeros(16);
                    }
                    if (type_name == "ArrayProperty")
                        name(target, type.at("parameters")[0].at("name").get<std::string>());
                    target.put<std::uint8_t>(0);
                }
                if (type_name != "BoolProperty")
                    target.append(payload);
            };
            std::map<std::string, int> references = {{"None", 0}};
            for (std::size_t i = 0; i < model.exports.size(); ++i)
                references[model.exports[i].path] = int(i + 1);
            auto fields = [&](const auto &self, const Json &props) -> Bytes {
                Bytes out;
                for (auto it = props.begin(); it != props.end(); ++it) {
                    const auto &t = it->at("type"), &v = it->at("value");
                    const auto type = t.at("name").get<std::string>();
                    Bytes payload;
                    if (type == "NameProperty")
                        name(payload, v.get<std::string>());
                    else if (type == "ObjectProperty")
                        payload.i(references.at(v.get<std::string>()));
                    else if (type == "StructProperty")
                        for (const auto &x : v)
                            payload.put<double>(x.get<double>());
                    else if (type == "ArrayProperty") {
                        payload.i(int(v.size()));
                        const auto inner = t.at("parameters")[0].at("name").get<std::string>();
                        if (inner == "StructProperty" && version < 1012) {
                            Bytes empty;
                            tag(payload, it.key(), t.at("parameters")[0], empty);
                        }
                        for (const auto &entry : v)
                            if (inner == "ObjectProperty")
                                payload.i(references.at(entry.get<std::string>()));
                            else
                                payload.append(self(self, entry));
                    } else if (type != "BoolProperty")
                        throw std::runtime_error("Unsupported attachment fixture property");
                    tag(out, it.key(), t, payload, type == "BoolProperty" && v.get<bool>());
                }
                name(out, "None");
                return out;
            };
            for (std::size_t i = 0; i < model.exports.size(); ++i) {
                name(object_names[i], model.exports[i].name);
                scripts[i] = fields(fields, model.exports[i].properties);
                if (i && version >= 1011) {
                    Bytes prefixed;
                    prefixed.put<std::uint8_t>(0);
                    prefixed.append(scripts[i]);
                    scripts[i] = std::move(prefixed);
                }
            }
            scripts[0].data.resize(scripts[0].data.size() - 8);
            root.append(scripts[0]);
            native.i(0);
            native.i(3);
            for (auto bone : {"root", "arm", "tip"}) {
                name(native, bone);
                native.i(std::string(bone) == "root" ? -1 : 0);
                native.string(bone);
            }
            native.i(3);
            for (double x : {0., 10., 20.})
                for (double component : {0., 0., 0., 1., x, 0., 0., 1., 1., 1.})
                    native.put(component);
            native.i(3);
            int index = 0;
            for (auto bone : {"root", "arm", "tip"}) {
                name(native, bone);
                native.i(index++);
            }
            const auto &sources = model.exports[0].native_data.at("retargeting").at("sources");
            native.i(int(sources.size()));
            for (auto it = sources.begin(); it != sources.end(); ++it) {
                name(native, it.key());
                name(native, it->at("poseName").get<std::string>());
                native.i(int(it->at("localPoses").size()));
                for (const auto &pose : it->at("localPoses"))
                    for (auto field : {"rotation", "translation", "scale"})
                        for (const auto &v : pose.at(field))
                            native.put<double>(v.get<double>());
                name(native, "None");
                if (version >= 1007)
                    name(native, "None");
                native.string("");
            }
            native.zeros(16);
            native.i(0);
            native.put<std::uint32_t>(0xdeadbeef);
        },
        -1, -1, -1, -1, 17);
    const auto owner_script_end = f.script.data.size() - 4;
    f.script.data.resize(owner_script_end);
    f.script.append(native);
    scripts[0] = f.script;
    for (std::size_t i = 1; i < scripts.size(); ++i)
        scripts[i].i(0);
    Bytes import, export_template;
    import.data.assign(f.header.data.begin() + f.export_offset - 40, f.header.data.begin() + f.export_offset);
    export_template.data.assign(f.header.data.begin() + f.export_offset, f.header.data.end());
    auto patch_name = [](Bytes &b, std::size_t offset, const Bytes &n) {
        std::copy(n.data.begin(), n.data.end(), b.data.begin() + offset);
    };
    patch_name(import, 20, owner_class);
    patch_name(import, 28, engine);
    f.header.data.resize(f.export_offset - 40);
    f.header.append(import);
    patch_name(import, 20, socket_class);
    f.header.append(import);
    f.export_offset = f.header.data.size();
    f.serial_offset_field = f.export_offset + 36;
    const auto header_size = f.export_offset + export_template.data.size() * scripts.size();
    std::size_t payload_offset = header_size;
    for (std::size_t i = 0; i < scripts.size(); ++i) {
        auto entry = export_template;
        entry.patch(0, i ? -2 : -1);
        entry.patch(12, model.exports[i].outer_index);
        patch_name(entry, 16, object_names[i]);
        const auto size = std::int64_t(scripts[i].data.size()), offset = std::int64_t(payload_offset);
        std::memcpy(entry.data.data() + 28, &size, 8);
        std::memcpy(entry.data.data() + 36, &offset, 8);
        if (version >= 1010) {
            const auto script_end = std::int64_t(i ? scripts[i].data.size() - 4 : owner_script_end);
            std::memcpy(entry.data.data() + entry.data.size() - 8, &script_end, 8);
        }
        f.header.append(entry);
        payload_offset += scripts[i].data.size();
    }
    f.header.patch(f.header_size_field, int(header_size));
    f.header.patch(f.export_count_field, int(scripts.size()));
    f.header.patch(f.export_offset_field, int(f.export_offset));
    f.header.patch(f.import_count_field, 2);
    f.script.data.clear();
    for (const auto &script : scripts)
        f.script.append(script);
    return f;
}
} // namespace cyuediff::test
