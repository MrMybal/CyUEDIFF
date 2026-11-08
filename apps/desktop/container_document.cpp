#include "container_document.hpp"
#include <algorithm>
#include <map>
namespace cyuediff::desktop {
Json ContainerDocument::report() const {
    Json pairs = Json::array();
    for (std::size_t i = 0; i < members.size(); ++i) {
        const auto &row = members[i];
        pairs.push_back(
            {{"key", row.key},
             {"state", member_states[i]},
             {"beforeId", row.before ? Json(input->entries()[*row.before].id) : Json(nullptr)},
             {"afterId", row.after ? Json(after->input->entries()[*row.after].id) : Json(nullptr)}});
    }
    Json schema = schema_manifest ? schema_manifest->to_json() : Json(nullptr);
    if (schema_manifest) {
        schema["mappingLoaded"] = schema_loaded;
        schema["contextSha256"] = zen ? zen->schema_context_sha256 : std::string{};
    }
    Json pixels = Json::array();
    for (const auto &p : previews)
        pixels.push_back({{"key", p.key},
                          {"object", p.object_path},
                          {"format", p.format},
                          {"mip", p.mip},
                          {"width", p.image.width},
                          {"height", p.image.height},
                          {"hdr", p.hdr.has_value()}});
    Json differences = Json::array();
    for (const auto &d : pixel_differences)
        differences.push_back(
            {{"key", d.key}, {"comparison", d.hdr ? d.hdr->to_json() : d.comparison.to_json()}});
    return {{"schema", "cyuediff.desktop-container/1"},
            {"physics", physics},
            {"physicsDifference", physics_difference},
            {"physicsEdges", physics_wire.edge_count()},
            {"physicsDiagnostics", physics_diagnostics},
            {"collisions", collisions},
            {"collisionDifference", collision_difference},
            {"collisionEdges", collision_wire.edge_count()},
            {"collisionDiagnostics", collision_diagnostics},
            {"meshes", meshes},
            {"meshDifferences", mesh_differences},
            {"meshPreviews", mesh_previews.size()},
            {"meshDiagnostics", mesh_diagnostics},
            {"textures", textures},
            {"texturePreviews", std::move(pixels)},
            {"textureDifferences", std::move(differences)},
            {"textureDiagnostics", texture_diagnostics},
            {"metadata", metadata},
            {"inspection", inspection},
            {"zen", zen ? zen->to_json() : Json(nullptr)},
            {"schemaContext", std::move(schema)},
            {"after", after ? after->report() : Json(nullptr)},
            {"members", std::move(pairs)},
            {"comparison", comparison},
            {"selectedComparison", selected_comparison}};
}
namespace {
std::shared_ptr<const ContainerDocument> load_single(const ContainerSource &source, const fs::path &codec,
                                                     const OperationControl &control) {
    if (source.path.empty())
        throw Error("desktop-container", "Choose a Pak or IoStore TOC");
    if (!source.max_payload_bytes || source.max_payload_bytes > 256ull * 1024 * 1024)
        throw Error("desktop-container", "Member read budget must be between 1 byte and 256 MiB");
    control.checkpoint("Indexing container");
    ContainerOptions index;
    index.control = control;
    index.max_entries = 100'000;
    index.max_index_bytes = 64ull * 1024 * 1024;
    auto document = std::make_shared<ContainerDocument>();
    if (document->after)
        for (const auto &before : document->mesh_previews) {
            control.check();
            auto after =
                std::find_if(document->after->mesh_previews.begin(), document->after->mesh_previews.end(),
                             [&](const auto &m) { return m.key == before.key; });
            if (after != document->after->mesh_previews.end())
                document->mesh_differences.push_back(
                    {{"key", before.key},
                     {"comparison", compare_meshes(*before.geometry, *after->geometry).to_json()}});
        }
    if (!document->physics.is_null() && document->after && !document->after->physics.is_null()) {
        CookedPhysicsOptions options;
        options.profile = source.physics_profile;
        options.control = control;
        document->physics_difference =
            compare_cooked_physics(document->physics, document->after->physics, options);
    }
    if (document->zen && document->after && document->after->zen &&
        (!document->collisions.is_null() || !document->after->collisions.is_null())) {
        CollisionOptions projection;
        projection.control = control;
        document->collision_difference =
            compare_collisions(*document->zen, *document->after->zen, projection).to_json();
    }
    document->source = source;
    document->input = std::make_shared<Container>(Container::open(source.path, index));
    document->metadata = document->input->metadata();
    document->protected_files = document->input->files();
    if (!codec.empty())
        document->protected_files.push_back(codec);
    std::optional<Container> global;
    if (!source.script_objects.empty()) {
        global = Container::open(source.script_objects, index);
        document->protected_files.insert(document->protected_files.end(), global->files().begin(),
                                         global->files().end());
    }
    if (!source.schema.empty()) {
        if (source.profile == ZenProfile::unspecified || document->metadata["kind"] != "iostore")
            throw Error("desktop-container", "Schema contexts require an explicit IoStore cooked profile");
        ZenSchemaManifestOptions limits;
        limits.control = control;
        document->schema_manifest = ZenSchemaManifest::read(source.schema, limits);
        if (document->schema_manifest->profile() != source.profile)
            throw Error("schema-context",
                        "Schema context profile disagrees with the selected cooked profile");
        document->protected_files.push_back(document->schema_manifest->source_path());
        document->protected_files.push_back(document->schema_manifest->mapping_path());
    }
    if (source.entry.empty()) {
        control.check();
        return document;
    }
    const auto &entries = document->input->entries();
    std::size_t found = entries.size();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        control.check();
        if (entries[i].id == source.entry || (!entries[i].path.empty() && entries[i].path == source.entry)) {
            if (found != entries.size())
                throw Error("container-entry", "Ambiguous member ID or virtual path");
            found = i;
        }
    }
    if (found == entries.size())
        throw Error("container-entry", "No matching container member");
    if (entries[found].deleted || entries[found].encrypted)
        throw Error("desktop-container", "Deleted or encrypted members cannot be inspected");
    if (document->schema_manifest && entries[found].chunk_type != 1)
        throw Error("desktop-container", "Schema contexts require a Zen export-bundle member");
    ContainerReadOptions read;
    read.control = control;
    read.max_payload_bytes = source.max_payload_bytes;
    if (!codec.empty())
        read.decoder = container_block_decoder(load_block_decoder(codec));
    auto bytes = document->input->read(found, read);
    const auto digest = sha256_bytes(bytes, control);
    std::string prefix;
    const char *hex = "0123456789abcdef";
    for (std::size_t i = 0; i < std::min<std::size_t>(64, bytes.size()); ++i) {
        if (i)
            prefix += ' ';
        prefix += hex[bytes[i] >> 4];
        prefix += hex[bytes[i] & 15];
    }
    document->inspection = {{"coverage", "decoded-bytes"},  {"entry", entries[found].to_json()},
                            {"decodedBytes", bytes.size()}, {"decodedSha256", digest},
                            {"prefixHex", prefix},          {"prefixLimit", 64}};
    if (document->metadata["kind"] == "iostore" && entries[found].chunk_type == 1 &&
        source.profile != ZenProfile::unspecified) {
        ZenReadOptions zen;
        zen.profile = source.profile;
        zen.control = control;
        zen.max_package_bytes = source.max_payload_bytes;
        zen.max_header_bytes = 16ull * 1024 * 1024;
        zen.max_text_bytes = 16ull * 1024 * 1024;
        zen.max_entries = 100'000;
        if (global) {
            std::size_t scripts = global->entries().size();
            for (std::size_t i = 0; i < global->entries().size(); ++i) {
                control.check();
                if (global->entries()[i].chunk_type == 5 && !global->entries()[i].deleted) {
                    if (scripts != global->entries().size())
                        throw Error("zen-entry", "Ambiguous global script object table");
                    scripts = i;
                }
            }
            if (scripts == global->entries().size())
                throw Error("zen-entry", "No script object chunk in global TOC");
            zen.script_objects =
                std::make_shared<ScriptObjects>(ScriptObjects::parse(global->read(scripts, read), zen));
        }
        if (document->schema_manifest) {
            SchemaReadOptions mappings;
            mappings.control = control;
            mappings.decoder = read.decoder;
            mappings.max_bytes = mappings.max_text_bytes = 16ull * 1024 * 1024;
            mappings.max_entries = 100'000;
            zen.schema_context = document->schema_manifest->load(mappings);
            document->schema_loaded = true;
        }
        document->zen = ZenPackage::parse(bytes, zen);
        if (std::any_of(document->zen->exports.begin(), document->zen->exports.end(), [](const auto &e) {
                return (e.class_name == "/Script/Engine.BodySetup" ||
                        e.class_name == "/Script/Engine.SkeletalBodySetup") &&
                       !(e.flags & 0x30);
            })) {
            try {
                CollisionOptions projection;
                projection.control = control;
                document->collisions = collision_snapshot(*document->zen, projection);
                CollisionWireframeOptions wire;
                wire.collision = projection;
                document->collision_wire = collision_wireframe(*document->zen, wire);
            } catch (const Error &error) {
                if (error.code == "cancelled" || error.code == "container-integrity")
                    throw;
                document->collision_diagnostics.push_back(error.code + ": " + error.what());
            }
        }
        if (source.physics_profile != CookedPhysicsProfile::unspecified) {
            try {
                CookedPhysicsOptions options;
                options.profile = source.physics_profile;
                options.control = control;
                options.max_read_bytes = std::min(256ull * 1024 * 1024, source.max_payload_bytes * 4);
                options.max_output_bytes = 128ull * 1024 * 1024;
                auto physics = read_zen_cooked_physics(*document->input, found, zen, read, options);
                auto review = cooked_physics_review(physics, options);
                document->physics = std::move(physics);
                document->physics_model = std::move(review.model);
                document->physics_wire = std::move(review.wire);
            } catch (const Error &error) {
                if (error.code == "cancelled" || error.code == "container-integrity")
                    throw;
                document->physics_diagnostics.push_back(error.code + ": " + error.what());
            }
        }
        std::uint64_t mesh_output = 0;
        for (std::size_t i = 0; i < document->zen->exports.size(); ++i) {
            const auto &object = document->zen->exports[i];
            const bool skeletal = object.class_name == "/Script/Engine.SkeletalMesh";
            if ((!skeletal && object.class_name != "/Script/Engine.StaticMesh") || (object.flags & 0x30))
                continue;
            try {
                auto append = [&](const auto &mesh) {
                    if (mesh.package_sha256 != document->zen->sha256)
                        throw Error("container-integrity",
                                    "Cooked mesh package changed during document preparation");
                    auto metadata = mesh.to_json();
                    std::uint64_t bytes = metadata.dump().size() * 6;
                    std::vector<ContainerMeshPreview> previews;
                    std::vector<std::string> diagnostics;
                    for (std::size_t l = 0; l < mesh.lods.size(); ++l) {
                        control.check();
                        const auto &lod = mesh.lods[l];
                        if (!lod.geometry) {
                            diagnostics.push_back(object.path + " LOD " + std::to_string(l) + ": " +
                                                  lod.unavailable);
                            continue;
                        }
                        const auto &g = *lod.geometry;
                        bytes += std::uint64_t(g.vertices.size()) * sizeof(MeshVertex) +
                                 std::uint64_t(g.instances.size()) * sizeof(MeshInstance) +
                                 std::uint64_t(g.triangles.size()) * sizeof(MeshTriangle);
                        for (const auto &v : g.instances)
                            bytes += v.uvs.size() * sizeof(std::array<float, 2>);
                        if (g.skin) {
                            for (const auto &bone : g.skin->bones)
                                bytes += sizeof(MeshBone) + bone.name.size();
                            for (const auto &profile : g.skin->profiles) {
                                bytes += 128 + profile.first.size();
                                for (const auto &v : profile.second)
                                    bytes += 128 + v.second.size() * sizeof(MeshInfluence);
                            }
                        }
                        if (bytes > 256ull * 1024 * 1024 - mesh_output)
                            throw Error("cooked-mesh-limit",
                                        "Cooked mesh previews exceed document byte budget");
                        previews.push_back({object.path + " LOD " + std::to_string(l), object.path,
                                            static_cast<std::uint32_t>(l), lod.geometry});
                    }
                    if (bytes > 256ull * 1024 * 1024 - mesh_output)
                        throw Error("cooked-mesh-limit", "Cooked mesh metadata exceeds document byte budget");
                    mesh_output += bytes;
                    document->meshes.push_back(std::move(metadata));
                    document->mesh_previews.insert(document->mesh_previews.end(), previews.begin(),
                                                   previews.end());
                    document->mesh_diagnostics.insert(document->mesh_diagnostics.end(), diagnostics.begin(),
                                                      diagnostics.end());
                };
                if (skeletal) {
                    CookedSkeletalOptions native;
                    native.profile = source.skeletal_profile;
                    native.keep_mobile_min_lod_setting = source.skeletal_mobile_lod;
                    native.has_vertex_colors = source.skeletal_colors;
                    native.control = control;
                    native.max_read_bytes = std::min(256ull * 1024 * 1024, source.max_payload_bytes * 4);
                    native.max_output_bytes = 256ull * 1024 * 1024 - mesh_output;
                    append(read_zen_skeletal_mesh(*document->input, found, zen, read, native, i));
                } else {
                    CookedMeshOptions native;
                    native.profile = source.mesh_profile;
                    native.keep_mobile_min_lod_setting = source.keep_mobile_min_lod_setting;
                    native.control = control;
                    native.max_read_bytes = std::min(256ull * 1024 * 1024, source.max_payload_bytes * 4);
                    native.max_output_bytes = 256ull * 1024 * 1024 - mesh_output;
                    append(read_zen_static_mesh(*document->input, found, zen, read, native, i));
                }
            } catch (const Error &error) {
                if (error.code == "cancelled" || error.code == "container-integrity")
                    throw;
                document->mesh_diagnostics.push_back(object.path + ": " + error.code + ": " + error.what());
            }
        }
        std::uint64_t preview_bytes = 0;
        std::optional<CookedTextureReader> native_reader;
        const auto preview_limit = std::min(256ull * 1024 * 1024, source.max_payload_bytes * 4);
        for (std::size_t i = 0; i < document->zen->exports.size(); ++i) {
            const auto &object = document->zen->exports[i];
            if (object.class_name != "/Script/Engine.Texture2D" || (object.flags & 0x30))
                continue;
            try {
                if (!native_reader) {
                    CookedTextureOptions native;
                    native.control = control;
                    native.max_payload_bytes = source.max_payload_bytes;
                    native.max_read_bytes = std::min(256ull * 1024 * 1024, source.max_payload_bytes * 4);
                    native_reader = CookedTextureReader::open(*document->input, found, zen, read, native);
                    if (native_reader->package().sha256 != document->zen->sha256)
                        throw Error("container-integrity",
                                    "Cooked texture package changed during document preparation");
                }
                auto texture = native_reader->read(i);
                document->textures.push_back(texture.to_json());
                for (const auto &platform : texture.platforms)
                    for (std::size_t m = 0; m < platform.mips.size(); ++m) {
                        const auto &mip = platform.mips[m];
                        try {
                            if (!mip.loaded)
                                throw Error("cooked-texture-unavailable",
                                            "Cooked mip is unavailable: " + mip.unavailable);
                            const auto hdr =
                                gpu_pixel_format_is_hdr(unreal_gpu_pixel_format(platform.format));
                            const auto size = std::uint64_t(mip.width) * mip.height * (hdr ? 20 : 4);
                            if (size > preview_limit - preview_bytes)
                                throw Error("cooked-texture-limit",
                                            "Cooked pixel previews exceed document byte budget");
                            GpuDecodeOptions gpu;
                            gpu.control = control;
                            gpu.max_output_bytes = preview_limit - preview_bytes;
                            ContainerTexturePreview preview;
                            preview.object_path = texture.object_path;
                            preview.format = platform.format;
                            preview.mip = static_cast<std::uint32_t>(m);
                            preview.key =
                                texture.object_path + " [" + platform.format + "] mip " + std::to_string(m);
                            if (hdr) {
                                preview.hdr = decode_cooked_mip_hdr(platform, m, gpu);
                                preview.image = tone_map_hdr(*preview.hdr, 0, control);
                            } else
                                preview.image = decode_cooked_mip(platform, m, gpu);
                            preview_bytes += size;
                            document->previews.push_back(std::move(preview));
                        } catch (const Error &error) {
                            if (error.code == "cancelled")
                                throw;
                            document->texture_diagnostics.push_back(texture.object_path + " mip " +
                                                                    std::to_string(m) + ": " + error.code +
                                                                    ": " + error.what());
                        }
                    }
            } catch (const Error &error) {
                if (error.code == "cancelled" || error.code == "container-integrity")
                    throw;
                document->texture_diagnostics.push_back(object.path + ": " + error.code + ": " +
                                                        error.what());
            }
        }
    }
    control.checkpoint("Document ready");
    return document;
}
} // namespace
std::shared_ptr<const ContainerDocument> load_container_document(const ContainerSource &source,
                                                                 const fs::path &codec,
                                                                 const OperationControl &control) {
    if (source.after_path.empty()) {
        if (source.compare_payloads || !source.after_script_objects.empty() ||
            source.after_profile != ZenProfile::unspecified || !source.after_schema.empty() ||
            source.after_physics_profile != CookedPhysicsProfile::unspecified ||
            source.after_skeletal_profile != CookedSkeletalProfile::unspecified ||
            source.after_skeletal_mobile_lod.has_value() || source.after_skeletal_colors.has_value() ||
            source.after_mesh_profile != CookedMeshProfile::unspecified ||
            source.after_keep_mobile_min_lod_setting.has_value())
            throw Error("desktop-container", "After container options require an after container");
        return load_single(source, codec, control);
    }
    if (!source.max_comparison_bytes || source.max_comparison_bytes > 1024ull * 1024 * 1024)
        throw Error("desktop-container", "Comparison budget must be between 1 byte and 1 GiB");
    auto left = source;
    left.after_path.clear();
    left.after_script_objects.clear();
    left.after_schema.clear();
    left.after_profile = ZenProfile::unspecified;
    left.after_physics_profile = CookedPhysicsProfile::unspecified;
    left.after_skeletal_profile = CookedSkeletalProfile::unspecified;
    left.after_skeletal_mobile_lod.reset();
    left.after_skeletal_colors.reset();
    left.after_mesh_profile = CookedMeshProfile::unspecified;
    left.after_keep_mobile_min_lod_setting.reset();
    left.compare_payloads = false;
    left.entry.clear();
    auto right = left;
    right.path = source.after_path;
    right.script_objects = source.after_script_objects;
    right.schema = source.after_schema;
    right.profile = source.after_profile;
    right.physics_profile = source.after_physics_profile;
    right.skeletal_profile = source.after_skeletal_profile;
    right.skeletal_mobile_lod = source.after_skeletal_mobile_lod;
    right.skeletal_colors = source.after_skeletal_colors;
    right.mesh_profile = source.after_mesh_profile;
    right.keep_mobile_min_lod_setting = source.after_keep_mobile_min_lod_setting;
    auto document = std::make_shared<ContainerDocument>(*load_single(left, codec, control));
    document->after = load_single(right, codec, control);
    document->members = pair_container_entries(*document->input, *document->after->input, control);
    std::optional<std::size_t> selected;
    for (std::size_t i = 0; i < document->members.size(); ++i) {
        control.check();
        const auto &row = document->members[i];
        document->member_states.push_back(!row.before ? "added" : (!row.after ? "removed" : "unchecked"));
        auto matches = [&](const ContainerDocument &side, const std::optional<std::size_t> &entry) {
            return entry && (side.input->entries()[*entry].id == source.entry ||
                             (!side.input->entries()[*entry].path.empty() &&
                              side.input->entries()[*entry].path == source.entry));
        };
        if (!source.entry.empty() && (row.key == source.entry || matches(*document, row.before) ||
                                      matches(*document->after, row.after))) {
            if (selected)
                throw Error("container-entry", "Ambiguous comparison member ID or virtual path");
            selected = i;
        }
    }
    if (!source.entry.empty() && !selected)
        throw Error("container-entry", "No matching live comparison member");
    if (selected) {
        const auto &pair = document->members[*selected];
        if (pair.before) {
            left.entry = document->input->entries()[*pair.before].id;
            const auto loaded = load_single(left, codec, control);
            document->inspection = loaded->inspection;
            document->zen = loaded->zen;
            document->schema_loaded = loaded->schema_loaded;
            document->physics = loaded->physics;
            document->physics_model = loaded->physics_model;
            document->physics_wire = loaded->physics_wire;
            document->physics_diagnostics = loaded->physics_diagnostics;
            document->collisions = loaded->collisions;
            document->collision_wire = loaded->collision_wire;
            document->collision_diagnostics = loaded->collision_diagnostics;
            document->meshes = loaded->meshes;
            document->mesh_previews = loaded->mesh_previews;
            document->mesh_diagnostics = loaded->mesh_diagnostics;
            document->textures = loaded->textures;
            document->previews = loaded->previews;
            document->texture_diagnostics = loaded->texture_diagnostics;
        }
        if (pair.after) {
            right.entry = document->after->input->entries()[*pair.after].id;
            document->after = load_single(right, codec, control);
        }
        const auto &b = document->inspection;
        const auto &a = document->after->inspection;
        const std::string state =
            !pair.before
                ? "added"
                : (!pair.after ? "removed"
                               : (b.at("decodedSha256") == a.at("decodedSha256") ? "unchanged" : "modified"));
        document->member_states[*selected] = state;
        document->selected_comparison = {
            {"schema", "cyuediff.desktop-container-member-diff/1"},
            {"key", pair.key},
            {"state", state},
            {"coverage", "decoded-bytes"},
            {"externalBulkCompared", false},
            {"zen", document->zen && document->after->zen
                        ? compare_zen_packages(*document->zen, *document->after->zen, control)
                        : Json(nullptr)}};
    }
    if (source.compare_payloads) {
        ContainerReadOptions read;
        read.control = control;
        read.max_payload_bytes = source.max_payload_bytes;
        read.max_comparison_bytes = source.max_comparison_bytes;
        if (!codec.empty())
            read.decoder = container_block_decoder(load_block_decoder(codec));
        document->comparison = compare_containers(*document->input, *document->after->input, read);
        std::map<std::string, std::string> changes;
        for (const auto &change : document->comparison.at("changes"))
            changes.emplace(change.at("key").get<std::string>(), change.at("change").get<std::string>());
        for (std::size_t i = 0; i < document->members.size(); ++i) {
            control.check();
            auto found = changes.find(document->members[i].key);
            document->member_states[i] = found == changes.end() ? "unchanged" : found->second;
        }
    }
    if (document->after)
        for (const auto &before : document->previews) {
            const auto after =
                std::find_if(document->after->previews.begin(), document->after->previews.end(),
                             [&](const auto &p) { return p.key == before.key; });
            if (after == document->after->previews.end())
                continue;
            ContainerTextureDifference difference;
            difference.key = before.key;
            const bool same =
                before.image.width == after->image.width && before.image.height == after->image.height;
            if (before.hdr && after->hdr) {
                auto hdr = compare_hdr_images(*before.hdr, *after->hdr, 0, same, control);
                difference.comparison = {hdr.comparable,
                                         hdr.same_size,
                                         hdr.before_width,
                                         hdr.before_height,
                                         hdr.after_width,
                                         hdr.after_height,
                                         hdr.changed_pixels,
                                         0,
                                         0,
                                         0,
                                         std::move(hdr.heatmap)};
                difference.comparison.changed_bounds = hdr.changed_bounds;
                difference.hdr = std::move(hdr);
            } else
                difference.comparison = compare_images(before.image, after->image, 0, same, control);
            document->pixel_differences.push_back(std::move(difference));
        }
    if (document->after)
        for (const auto &before : document->mesh_previews) {
            control.check();
            auto after =
                std::find_if(document->after->mesh_previews.begin(), document->after->mesh_previews.end(),
                             [&](const auto &m) { return m.key == before.key; });
            if (after != document->after->mesh_previews.end())
                document->mesh_differences.push_back(
                    {{"key", before.key},
                     {"comparison", compare_meshes(*before.geometry, *after->geometry).to_json()}});
        }
    if (!document->physics.is_null() && document->after && !document->after->physics.is_null()) {
        CookedPhysicsOptions options;
        options.profile = source.physics_profile;
        options.control = control;
        document->physics_difference =
            compare_cooked_physics(document->physics, document->after->physics, options);
    }
    if (document->zen && document->after && document->after->zen &&
        (!document->collisions.is_null() || !document->after->collisions.is_null())) {
        CollisionOptions projection;
        projection.control = control;
        document->collision_difference =
            compare_collisions(*document->zen, *document->after->zen, projection).to_json();
    }
    document->source = source;
    document->protected_files.insert(document->protected_files.end(),
                                     document->after->protected_files.begin(),
                                     document->after->protected_files.end());
    control.checkpoint("Document ready");
    return document;
}
} // namespace cyuediff::desktop
