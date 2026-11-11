#include <cyuediff/content_hash.hpp>
#include <cyuediff/virtualized.hpp>
#include "../src/mesh.hpp"
#include "mesh_description_fixture.hpp"
using namespace cyuediff::test::mesh_fixture;
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
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
        bad.bytes.patch<std::uint32_t>(data.type, 8);
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
        {
            Bytes encoded;
            auto big = [&](std::uint64_t v, unsigned n) {
                for (unsigned i = n; i > 0; --i)
                    encoded.data.push_back(std::uint8_t(v >> ((i - 1) * 8)));
            };
            big(0xb7756362, 4);
            encoded.zeros(4);
            encoded.zeros(4);
            big(1, 4);
            big(data.bytes.data.size(), 8);
            big(64 + data.bytes.data.size(), 8);
            const auto hash = blake3_bytes(data.bytes.data);
            for (std::size_t i = 0; i < 32; ++i)
                encoded.data.push_back(std::uint8_t(std::stoul(hash.substr(i * 2, 2), nullptr, 16)));
            std::uint32_t crc = ~0u;
            for (std::size_t i = 8; i < 64; ++i) {
                crc ^= encoded.data[i];
                for (unsigned k = 0; k < 8; ++k)
                    crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
            }
            crc = ~crc;
            for (unsigned i = 0; i < 4; ++i)
                encoded.data[4 + i] = std::uint8_t(crc >> (24 - i * 8));
            encoded.data.insert(encoded.data.end(), data.bytes.data.begin(), data.bytes.data.end());
            const auto editor_file = root / "editor-bulk.bin";
            auto editor_record = [&](std::uint32_t flags) {
                Bytes record;
                record.put(flags);
                record.zeros(16);
                for (std::size_t i = 0; i < 20; ++i)
                    record.data.push_back(std::uint8_t(std::stoul(hash.substr(i * 2, 2), nullptr, 16)));
                record.put<std::int64_t>(data.bytes.data.size());
                if (!(flags & (1u | 512u | 1024u)))
                    record.put<std::int64_t>(64);
                record.zeros(64 - record.data.size());
                record.data.insert(record.data.end(), encoded.data.begin(), encoded.data.end());
                record.save(editor_file);
            };
            Package editor_package;
            editor_package.source = editor_file;
            editor_package.summary.custom_versions["697dd581e64f41abaa4a51ecbeb7b628"] = 59;
            for (std::uint32_t flags : {0u, 64u, 128u, 2048u, 64u | 128u | 2048u}) {
                editor_record(flags);
                ReadOptions read;
                detail::Archive archive(editor_file, read);
                auto result = detail::read_editor_bulk(archive, editor_package, read);
                check(result.flags == flags && result.payload.storage == "local" &&
                          result.payload.offset == 64,
                      "Transient editor flags retain provenance without changing storage selection");
                BulkDecodeOptions verified;
                verified.verify_raw_hash = true;
                check(decode_bulk_payload(result.payload, verified) == data.bytes.data,
                      "Transient editor state still requires the exact CRC/header and raw hash");
            }
            for (std::uint32_t flags : {4u | 64u, 8u | 128u, 256u | 2048u}) {
                editor_record(flags);
                ReadOptions read;
                detail::Archive archive(editor_file, read);
                check(detail::read_editor_bulk(archive, editor_package, read).payload.storage ==
                          "legacy-unsupported",
                      "Actual legacy/workspace flags remain unsupported");
            }
            editor_record(1u | 64u);
            {
                ReadOptions read;
                detail::Archive archive(editor_file, read);
                check(detail::read_editor_bulk(archive, editor_package, read).payload.storage ==
                          "virtualized",
                      "Transient registration does not override virtualized storage");
            }
            const auto store = root / "payloads";
            const auto file = store / virtualized_payload_relative_path(hash.substr(0, 40));
            fs::create_directories(file.parent_path());
            encoded.save(file);
            auto virtualized = source;
            virtualized.payload.storage = "virtualized";
            virtualized.payload.raw_bytes = data.bytes.data.size();
            virtualized.payload.content_id = hash.substr(0, 40);
            MeshDecodeOptions virtual_options;
            virtual_options.virtualized_payload_root = store;
            check(decode_mesh(virtualized, virtual_options).to_json() == decode(data).to_json(),
                  "Public virtualized mesh geometry equals local MeshDescription");
            MeshDecodeOptions host_store;
            host_store.virtualized_payload_source = [&](const std::string &id) -> ByteSourcePtr {
                return id == hash.substr(0, 40)
                           ? memory_source(std::vector<std::uint8_t>(encoded.data.begin(), encoded.data.end()),
                                           "host-store")
                           : nullptr;
            };
            check(decode_mesh(virtualized, host_store).to_json() == decode(data).to_json(),
                  "Host virtualized stores supply payload bytes without a filesystem root");
            auto absent = virtualized;
            absent.payload.content_id = std::string(39, '0') + "1";
            error([&] { decode_mesh(absent, host_store); }, "virtualized-missing");
            host_store.virtualized_payload_source = [&](const std::string &) -> ByteSourcePtr {
                auto bytes = std::vector<std::uint8_t>(encoded.data.begin(), encoded.data.end());
                bytes.back() ^= 1;
                return memory_source(std::move(bytes));
            };
            error([&] { decode_mesh(virtualized, host_store); }, "bulk-hash");
        }
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
        asset.coverage = "properties";
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
        const auto error_mode = GetThreadErrorMode();
        error([&] { load_block_decoder(path); }, "codec-load");
        check(GetThreadErrorMode() == error_mode, "Failed codec load restores the caller thread error mode");
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
