#include "../src/mesh.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
using namespace cyuediff;
namespace {
int checks = 0;
void check(bool good, const char *message) {
    ++checks;
    if (!good)
        throw std::runtime_error(message);
}
template <class F> void error(F fn, const std::string &code) {
    try {
        fn();
    } catch (const Error &e) {
        check(e.code == code, ("Expected " + code + ", got " + e.code).c_str());
        return;
    }
    throw std::runtime_error("Expected error " + code);
}
struct Bytes {
    std::vector<std::uint8_t> data;
    template <class T> void put(T v) {
        auto p = reinterpret_cast<const std::uint8_t *>(&v);
        data.insert(data.end(), p, p + sizeof(T));
    }
    void i(int v) {
        put<std::int32_t>(v);
    }
    void f(float v) {
        put(v);
    }
    void zeros(std::size_t n) {
        data.insert(data.end(), n, 0);
    }
    void string(const std::string &s) {
        i(static_cast<int>(s.size() + 1));
        data.insert(data.end(), s.begin(), s.end());
        data.push_back(0);
    }
    void wide(const std::u16string &s) {
        i(-static_cast<int>(s.size() + 1));
        for (auto c : s)
            put(c);
        put<char16_t>(0);
    }
    template <class T> void patch(std::size_t at, T v) {
        std::memcpy(data.data() + at, &v, sizeof(T));
    }
    void save(const fs::path &path) const {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    }
};
struct Fixture {
    Bytes bytes;
    std::size_t vertex = 0, vertex_extent = 0, reference = 0, triangle = 0, sparse = 0, numeric = 0, type = 0,
                utf = 0;
};
Fixture fixture() {
    Fixture f;
    auto &b = f.bytes;
    b.i(7);
    auto element = [&](const char *name, int bits, std::uint32_t mask, int holes, int attributes) {
        b.string(name);
        b.i(1);
        b.i(bits);
        if (bits)
            b.put(mask);
        b.i(holes);
        b.i(bits);
        b.i(attributes);
    };
    auto fixed = [&](const char *name, int type, int extent, int elements,
                     const std::vector<std::vector<float>> &floats, const std::vector<int> &integers = {}) {
        b.string(name);
        auto type_at = b.data.size();
        b.i(type);
        b.i(extent);
        b.i(elements);
        b.i(integers.empty() ? static_cast<int>(floats.size()) : 1);
        std::size_t offset = 0;
        int components = type < 3 ? 4 - type : 1;
        auto channel = [&]() {
            b.i(extent);
            b.i(type == 5 ? 1 : components * 4);
            b.i(elements * extent);
            offset = b.data.size();
        };
        if (!integers.empty()) {
            channel();
            for (auto i : integers)
                b.i(i);
        } else
            for (const auto &values : floats) {
                channel();
                for (float v : values)
                    b.f(v);
            }
        if (type < 4)
            for (int i = 0; i < components; ++i)
                b.f(0);
        else
            b.i(-1);
        b.i(0);
        if (std::string(name) == "Position ") {
            f.vertex = offset;
            f.vertex_extent = type_at + 4;
            f.type = type_at;
        }
        return offset;
    };
    element("Vertices", 5, 0x15, 2, 1);
    f.sparse = 4 + 4 + 9 + 4 + 4 + 4;
    fixed("Position ", 1, 1, 5, {{0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0}});
    element("VertexInstances", 3, 7, 0, 3);
    f.reference = fixed("VertexIndex", 4, 1, 3, {}, {0, 2, 4});
    f.numeric = fixed("Normal", 1, 1, 3, {{0, 0, 1, 0, 0, 1, 0, 0, 1}});
    fixed("TextureCoordinate", 2, 1, 3, {{0, 0, 1, 0, 0, 1}, {.5f, .5f, 1, .5f, .5f, 1}});
    element("Triangles", 3, 4, 2, 3);
    f.triangle = fixed("VertexInstanceIndex", 4, 3, 3, {}, {-1, -1, -1, -1, -1, -1, 0, 1, 2});
    fixed("PolygonIndex", 4, 1, 3, {}, {-1, -1, 7});
    fixed("PolygonGroupIndex", 4, 1, 3, {}, {-1, -1, 3});
    element("Polygons", 8, 128, 7, 0);
    element("PolygonGroups", 4, 8, 3, 1);
    b.string("ImportedMaterialSlotName");
    b.i(6);
    b.i(1);
    b.i(4);
    b.i(1);
    b.i(1);
    b.i(4);
    for (int i = 0; i < 3; ++i)
        b.string("None");
    f.utf = b.data.size() + 4;
    b.wide(u"Matériau");
    b.string("None");
    b.i(32);
    element("Edges", 2, 3, 0, 1);
    b.string("CustomConnections");
    b.i(4);
    b.i(0);
    b.i(2);
    b.i(1);
    b.i(1);
    b.i(3);
    b.i(5);
    b.i(6);
    b.i(7);
    b.i(2);
    b.i(0);
    b.i(1);
    b.i(1);
    b.i(2);
    b.i(1);
    b.i(2);
    b.i(2);
    b.i(-1);
    b.i(-1);
    b.i(0);
    element("UVs", 0, 0, 0, 0);
    return f;
}
MeshSource profile() {
    MeshSource s;
    s.object_path = "Mesh.Source";
    s.ue5_version = 1009;
    s.custom_versions = {{"697dd581e64f41abaa4a51ecbeb7b628", 118},
                         {"e4b068edf49442e9a231da0b2e46bb41", 40},
                         {"9c54d522a8264fbe9421074661b482d0", 44},
                         {"9dffbcd6494f0158e22112823c92a888", 10}};
    return s;
}
Bytes compressed() {
    Bytes b;
    auto big = [&](std::uint64_t v, unsigned n) {
        for (unsigned i = n; i > 0; --i)
            b.data.push_back(static_cast<std::uint8_t>(v >> ((i - 1) * 8)));
    };
    big(0xb7756362, 4);
    b.zeros(4);
    b.data.push_back(3);
    b.zeros(2);
    b.data.push_back(2);
    big(2, 4);
    big(8, 8);
    big(74, 8);
    b.zeros(32);
    big(1, 4);
    big(1, 4);
    b.data.push_back('a');
    b.data.push_back('b');
    return b;
}
void checksum(Bytes &b) {
    std::uint32_t crc = ~0u;
    for (std::size_t i = 8; i < 72; ++i) {
        crc ^= b.data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    crc ^= ~0u;
    for (int i = 0; i < 4; ++i)
        b.data[4 + i] = static_cast<std::uint8_t>(crc >> (24 - 8 * i));
}
} // namespace
int main() {
    auto root =
        fs::current_path() /
        (".test-tmp-mesh-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    try {
        auto source = profile();
        auto data = fixture();
        MeshDecodeOptions options;
        auto decode = [&](const Fixture &f) {
            return detail::decode_mesh_description(f.bytes.data, source, options);
        };
        auto mesh = decode(data);
        unsigned cancel_checks = 0;
        options.control.cancelled = [&] { return ++cancel_checks >= 12; };
        error([&] { decode(data); }, "cancelled");
        options.control = {};

        check(mesh.vertices.size() == 3 && mesh.instances.size() == 3 && mesh.triangles.size() == 1,
              "Sparse geometry counts");
        check(mesh.vertices[1].id == 2 && mesh.vertices[2].id == 4 && mesh.triangles[0].id == 2,
              "Stable sparse element IDs retained");
        check(mesh.instances[2].vertex == 4 &&
                  mesh.triangles[0].instances == std::array<std::uint32_t, 3>{0, 1, 2},
              "References resolved through vertex instances");
        check(mesh.instances[1].uvs.size() == 2 && mesh.instances[1].uvs[1][1] == .5f,
              "Multiple UV channels decoded");
        check(mesh.instances[0].normal[2] == 1, "Native normals retained");
        check(mesh.material_slots.at(3) == u8"Matériau", "UTF16 material slot name");
        check(mesh.bounds.at("max") == Json::array({1.0, 1.0, 0.0}), "Bounds derived from active positions");
        check(mesh.attributes.at("Edges")[0].at("attributes").contains("CustomConnections"),
              "Unknown unbounded attribute retained as fingerprint");
        check(mesh.to_json().at("vertices").contains("4") && !mesh.to_json().at("vertices").contains("1"),
              "Snapshot keyed by sparse IDs");
        check(compare_meshes(mesh, mesh).changes.empty(), "Identical geometry comparison");
        auto changed = mesh;
        changed.vertices[1].position[0] = 2;
        auto diff = compare_meshes(mesh, changed);
        check(diff.changes.size() == 1 && diff.changes[0].at("path") == "/vertices/2/position/0",
              "Position change has stable semantic path");
        changed = mesh;
        changed.material_slots[3] = "Other";
        check(compare_meshes(mesh, changed).changes[0].at("path") == "/materialSlots/3",
              "Material group diff");
        changed = mesh;
        changed.instances[0].uvs[1][0] = .75f;
        check(compare_meshes(mesh, changed).changes[0].at("path") == "/instances/0/uvs/1/0",
              "UV channel diff");
        auto obj = root / fs::u8path("géométrie.obj");
        write_obj(obj, mesh);
        std::ifstream file(obj, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(file)), {});
        file.close();
        check(text.find("f 1/1/1 2/2/2 3/3/3") != std::string::npos &&
                  text.find("g section_3") != std::string::npos,
              "OBJ uses dense indices and group IDs");
        changed = mesh;
        changed.triangles[0].instances[0] = 99;
        error([&] { write_obj(obj, changed); }, "mesh-reference");
        bool bounded = true;
        for (std::size_t n = 0; n < data.bytes.data.size(); n += 29) {
            auto truncated = data;
            truncated.bytes.data.resize(n);
            try {
                decode(truncated);
                bounded = false;
                break;
            } catch (const Error &) {
            }
        }
        check(bounded, "Truncated mesh payloads fail with structured errors");
        auto bad = data;
        bad.bytes.patch<std::int32_t>(data.reference, 1);
        error([&] { decode(bad); }, "mesh-reference");
        bad = data;
        bad.bytes.patch<std::int32_t>(data.triangle + 6 * 4, 99);
        error([&] { decode(bad); }, "mesh-reference");
        bad = data;
        bad.bytes.patch<std::uint32_t>(data.vertex_extent, 2);
        error([&] { decode(bad); }, "mesh-attribute");
        bad = data;
        bad.bytes.patch<std::uint32_t>(data.type, 7);
        error([&] { decode(bad); }, "mesh-attribute-type");
        bad = data;
        bad.bytes.patch<float>(data.numeric, std::numeric_limits<float>::quiet_NaN());
        error([&] { decode(bad); }, "mesh-number");
        bad = data;
        bad.bytes.patch<std::uint32_t>(data.sparse, 0);
        error([&] { decode(bad); }, "mesh-sparse");
        bad = data;
        bad.bytes.patch<char16_t>(data.utf, 0xdc00);
        error([&] { decode(bad); }, "mesh-string");
        bad = data;
        bad.bytes.data.push_back(0);
        error([&] { decode(bad); }, "mesh-trailing");
        bad = data;
        bad.bytes.data.resize(20);
        error([&] { decode(bad); }, "mesh-bounds");
        options.max_elements = 4;
        error([&] { decode(data); }, "limit");
        options = {};
        options.max_attribute_values = 10;
        error([&] { decode(data); }, "limit");
        options = {};
        source.custom_versions["697dd581e64f41abaa4a51ecbeb7b628"] = 120;
        error([&] { decode(data); }, "mesh-profile");
        source = profile();
        auto bulk = compressed();
        checksum(bulk);
        auto path = root / "bulk.bin";
        bulk.save(path);
        BulkPayload payload;
        payload.file = path;
        payload.offset = 0;
        payload.raw_bytes = 8;
        payload.stored_bytes = 74;
        payload.storage = "local";
        payload.method = 3;
        error([&] { decode_bulk_payload(payload); }, "texture-codec");
        BulkDecodeOptions bulk_options;
        int calls = 0;
        bulk_options.decoder = [&](std::uint32_t method, const std::uint8_t *input, std::size_t size,
                                   std::uint8_t *output, std::size_t capacity) {
            ++calls;
            check(method == 3 && size == 1 && capacity == 4, "Block decoder receives bounded spans");
            std::fill_n(output, capacity, input[0]);
            return capacity;
        };
        check(decode_bulk_payload(payload, bulk_options) ==
                      std::vector<std::uint8_t>({'a', 'a', 'a', 'a', 'b', 'b', 'b', 'b'}) &&
                  calls == 2,
              "Compressed block table decoded through host callback");
        bulk_options.decoder = [](std::uint32_t, const std::uint8_t *, std::size_t, std::uint8_t *,
                                  std::size_t capacity) { return capacity - 1; };
        error([&] { decode_bulk_payload(payload, bulk_options); }, "bulk-codec");
        auto malformed = bulk;
        malformed.data[71] = 5;
        checksum(malformed);
        malformed.save(path);
        error([&] { decode_bulk_payload(payload, bulk_options); }, "bulk-layout");
        malformed = bulk;
        malformed.data[11] = 1;
        checksum(malformed);
        malformed.save(path);
        error([&] { decode_bulk_payload(payload, bulk_options); }, "bulk-layout");
        bulk.save(path);
        bulk_options.max_payload_bytes = 73;
        error([&] { decode_bulk_payload(payload, bulk_options); }, "limit");
        payload.offset = 9999;
        error([&] { decode_bulk_payload(payload); }, "bounds");
        payload.offset = 0;
        payload.sha256 = std::string(64, '0');
        error([&] { decode_bulk_payload(payload); }, "source-changed");
        Bytes native;
        native.i(0);
        native.i(1);
        native.zeros(36);
        native.put<std::int64_t>(8);
        native.zeros(16);
        native.i(1);
        native.save(path);
        Package package;
        package.source = path;
        package.summary.custom_versions = source.custom_versions;
        ObjectExport object;
        object.path = "Mesh.Source";
        object.class_name = "/Script/Engine.StaticMeshDescriptionBulkData";
        package.exports.push_back(object);
        ReadOptions read;
        {
            detail::Archive archive(path, read);
            auto parsed = detail::decode_mesh_source(archive, package, 0, 72, read);
            check(parsed.end == 72 && parsed.source.payload.storage == "virtualized",
                  "Mesh native source identifies virtualized storage");
        }
        {
            detail::Archive archive(path, read);
            error([&] { detail::decode_mesh_source(archive, package, 0, 71, read); }, "bounds");
        }
        package.summary.custom_versions.erase("9dffbcd6494f0158e22112823c92a888");
        native.data.resize(68);
        native.save(path);
        {
            detail::Archive archive(path, read);
            auto legacy = detail::decode_mesh_source(archive, package, 0, 68, read);
            check(legacy.end == 68 && legacy.native.at("guidIsHash") == false,
                  "Older enterprise profile omits hash flag");
        }
        package.mesh_sources.push_back(source);
        ObjectExport asset;
        asset.name = asset.path = "Mesh";
        asset.class_name = "/Script/Engine.StaticMesh";
        asset.properties = {
            {"SourceModels",
             {{"value", Json::array({{{"StaticMeshDescriptionBulkData", {{"value", "Mesh.Source"}}}}})}}}};
        package.exports.push_back(asset);
        check(package.mesh_source().object_path == "Mesh.Source", "Source LOD resolves its bulk object");
        error([&] { package.mesh_source("", 1); }, "mesh-lod");
        error([&] { package.mesh_source("missing"); }, "mesh-selection");
        check(package.mesh_snapshot().at("meshes").contains("Mesh"), "Static mesh metadata projection");
        auto after = package;
        after.exports.back().properties["LightMapResolution"] = {{"value", 128}};
        check(!compare(package, after).mesh_changes.empty(), "Build setting has specialized mesh diff");
#ifdef _WIN32
        error([&] { load_block_decoder(path); }, "codec-load");
#endif
        if (root.parent_path() != fs::current_path())
            throw std::runtime_error("Invalid fixture cleanup root");
        fs::remove_all(root);
        std::cout << checks << " mesh/bulk checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAILED: " << e.what() << "; fixtures: " << root << '\n';
        return 1;
    }
}
