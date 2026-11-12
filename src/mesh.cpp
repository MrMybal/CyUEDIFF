#include <cyuediff/cloth.hpp>
#include <cyuediff/mesh_overlaps.hpp>
#include <cyuediff/vertex_attributes.hpp>
#include <cyuediff/render_morphs.hpp>
#include <cyuediff/mesh_skin.hpp>
#include <cyuediff/mesh_morphs.hpp>
#include "mesh.hpp"
#include "atomic_output.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
namespace cyuediff::detail {
namespace {
const Json *value(const Json &props, const char *key) {
    auto it = props.find(key);
    if (it == props.end() || !it->is_object() || !it->contains("value"))
        return nullptr;
    return &it->at("value");
}
bool native_bool(Archive &a) {
    auto n = a.get<std::uint32_t>();
    if (n > 1)
        throw Error("mesh-boolean", "Invalid mesh native boolean", a.tell() - 4);
    return n != 0;
}
class Reader {
    const std::vector<std::uint8_t> &bytes_;

  public:
    std::size_t pos = 0;
    const MeshDecodeOptions &options;
    std::uint64_t values = 0;
    bool wide_transform = true, legacy_transform = false, transform_version_known = false;
    Reader(const std::vector<std::uint8_t> &bytes, const MeshDecodeOptions &opts)
        : bytes_(bytes), options(opts) {}
    void require(std::size_t n) const {
        options.control.check();
        if (pos > bytes_.size() || n > bytes_.size() - pos)
            throw Error("mesh-bounds", "Mesh data exceeds payload", pos);
    }
    void skip(std::size_t n) {
        require(n);
        pos += n;
    }
    template <class T> T get() {
        require(sizeof(T));
        std::array<std::uint8_t, sizeof(T)> b{};
        std::copy_n(bytes_.data() + pos, sizeof(T), b.data());
        pos += sizeof(T);
        const std::uint16_t little = 1;
        if (*reinterpret_cast<const std::uint8_t *>(&little) != 1)
            std::reverse(b.begin(), b.end());
        T v;
        std::memcpy(&v, b.data(), sizeof(T));
        return v;
    }
    std::uint32_t count(std::uint32_t limit) {
        auto n = get<std::int32_t>();
        if (n < 0 || static_cast<std::uint32_t>(n) > limit)
            throw Error("limit", "Mesh count exceeds configured limit", pos - 4);
        return static_cast<std::uint32_t>(n);
    }
    void budget(std::uint64_t n) {
        if (n > options.max_attribute_values - values)
            throw Error("limit", "Mesh attribute value budget exceeded", pos);
        values += n;
    }
    std::string string() {
        auto n = get<std::int32_t>();
        if (n == 0)
            return {};
        if (n == INT32_MIN || std::uint64_t(n < 0 ? -std::int64_t(n) : n) > 1048576)
            throw Error("limit", "Mesh string exceeds limit", pos - 4);
        if (n > 0) {
            require(static_cast<std::size_t>(n));
            if (bytes_[pos + n - 1] != 0)
                throw Error("mesh-string", "Unterminated mesh name", pos);
            std::string s(reinterpret_cast<const char *>(bytes_.data() + pos),
                          static_cast<std::size_t>(n - 1));
            pos += n;
            return s;
        }
        auto count = static_cast<std::uint32_t>(-n);
        std::u16string text;
        require(std::size_t(count) * 2);
        for (std::uint32_t i = 0; i < count; ++i)
            text.push_back(get<char16_t>());
        if (text.back() != 0)
            throw Error("mesh-string", "Unterminated UTF16 mesh name", pos);
        text.pop_back();
        std::string result;
        for (std::size_t i = 0; i < text.size(); ++i) {
            std::uint32_t cp = text[i];
            if (cp >= 0xd800 && cp <= 0xdbff) {
                if (i + 1 == text.size() || text[i + 1] < 0xdc00 || text[i + 1] > 0xdfff)
                    throw Error("mesh-string", "Invalid UTF16 surrogate", pos);
                cp = 0x10000 + ((cp - 0xd800) << 10) + (text[++i] - 0xdc00);
            } else if (cp >= 0xdc00 && cp <= 0xdfff)
                throw Error("mesh-string", "Invalid UTF16 surrogate", pos);
            if (cp < 0x80)
                result.push_back(static_cast<char>(cp));
            else if (cp < 0x800) {
                result.push_back(static_cast<char>(0xc0 | (cp >> 6)));
                result.push_back(static_cast<char>(0x80 | (cp & 63)));
            } else if (cp < 0x10000) {
                result.push_back(static_cast<char>(0xe0 | (cp >> 12)));
                result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
                result.push_back(static_cast<char>(0x80 | (cp & 63)));
            } else {
                result.push_back(static_cast<char>(0xf0 | (cp >> 18)));
                result.push_back(static_cast<char>(0x80 | ((cp >> 12) & 63)));
                result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 63)));
                result.push_back(static_cast<char>(0x80 | (cp & 63)));
            }
        }
        return result;
    }
    std::string hash(std::size_t begin, std::size_t end) const {
        return sha256_bytes(std::vector<std::uint8_t>(bytes_.begin() + begin, bytes_.begin() + end),
                            options.control);
    }
    std::size_t size() const {
        return bytes_.size();
    }
};
unsigned components(std::uint32_t type) {
    if (type < 3)
        return 4 - type;
    if (type < 6)
        return 1;
    return 0;
}
unsigned stride(std::uint32_t type) {
    if (type < 5)
        return components(type) * 4;
    if (type == 5)
        return 1;
    return 0;
}
Json scalar(Reader &r, std::uint32_t type) {
    if (type < 4) {
        Json v = Json::array();
        for (unsigned c = 0; c < components(type); ++c) {
            auto f = r.get<float>();
            if (!std::isfinite(f))
                throw Error("mesh-number", "Non-finite mesh attribute default", r.pos - 4);
            v.push_back(f);
        }
        return v;
    }
    if (type == 4)
        return r.get<std::int32_t>();
    if (type == 5) {
        auto b = r.get<std::uint32_t>();
        if (b > 1)
            throw Error("mesh-boolean", "Invalid mesh default boolean", r.pos - 4);
        return b != 0;
    }
    if (type == 6)
        return r.string();
    if (type == 7) {
        Json transform = Json::object();
        for (auto field : {std::make_pair("rotation", 4u), std::make_pair("translation", 3u),
                           std::make_pair("scale", 3u)}) {
            Json values = Json::array();
            for (unsigned i = 0; i < field.second; ++i) {
                const double value = r.wide_transform ? r.get<double>() : r.get<float>();
                if (!std::isfinite(value))
                    throw Error("mesh-number", "Non-finite MeshDescription transform", r.pos);
                values.push_back(value);
            }
            transform[field.first] = std::move(values);
        }
        return transform;
    }
    throw Error("mesh-attribute-type", "Unsupported mesh attribute type", r.pos);
}
struct Range {
    std::size_t offset = 0;
    std::uint32_t count = 0;
};
struct Channel {
    std::size_t offset = 0;
    std::uint32_t count = 0;
    std::vector<Range> ranges;
};
struct Attribute {
    std::uint32_t type = 0, extent = 0, elements = 0;
    std::vector<Channel> channels;
    Json metadata;
};
struct Container {
    std::vector<bool> allocated;
    std::uint32_t active = 0;
    std::map<std::string, Attribute> attributes;
};
using Elements = std::map<std::string, std::vector<Container>>;
Attribute read_attribute(Reader &r, std::uint32_t max_elements, bool retain_ranges) {
    Attribute a;
    a.type = r.get<std::uint32_t>();
    a.extent = r.get<std::uint32_t>();
    if (a.type == 7 && !r.transform_version_known)
        throw Error("mesh-profile", "Transform attributes require an explicit supported UE archive version",
                    r.pos - 8);
    if (a.type > 7)
        throw Error("mesh-attribute-type", "Unsupported mesh attribute type", r.pos - 8);
    if (a.extent > 64)
        throw Error("limit", "Mesh attribute extent exceeds limit", r.pos - 4);
    a.elements = r.count(max_elements);
    auto channels = r.count(32);
    Json descriptions = Json::array();
    for (std::uint32_t c = 0; c < channels; ++c) {
        auto begin = r.pos;
        Channel channel;
        if (a.extent) {
            if (r.get<std::uint32_t>() != a.extent)
                throw Error("mesh-attribute", "Attribute extent mismatch", r.pos - 4);
            if (a.type < 6 || (a.type == 7 && r.legacy_transform)) {
                const auto stored_stride = r.get<std::int32_t>();
                if (a.type == 7) {
                    if (stored_stride <= 0)
                        throw Error("mesh-attribute", "Invalid legacy transform element size", r.pos - 4);
                } else if (stored_stride != static_cast<int>(stride(a.type)))
                    throw Error("mesh-attribute", "Attribute element size mismatch", r.pos - 4);
            }
            channel.count = r.count(r.options.max_attribute_values);
            if (channel.count != std::uint64_t(a.elements) * a.extent)
                throw Error("mesh-attribute", "Attribute count does not match element extent", r.pos - 4);
            if (a.type == 7 && r.legacy_transform && channel.count)
                throw Error("mesh-profile", "Legacy bulk-serialized transform arrays must be empty",
                            r.pos - 4);
            r.budget(std::uint64_t(channel.count) * (a.type == 7 ? 10 : std::max(1u, components(a.type))));
            channel.offset = r.pos;
            if (a.type < 6)
                r.skip(std::size_t(channel.count) * stride(a.type));
            else
                for (std::uint32_t i = 0; i < channel.count; ++i)
                    scalar(r, a.type);
        } else {
            auto chunks = r.count(1 + (max_elements - 1) / 256);
            std::uint64_t entries = 0;
            for (std::uint32_t i = 0; i < chunks; ++i) {
                auto count = r.count(r.options.max_attribute_values);
                r.budget(std::uint64_t(count) * (a.type == 7 ? 10 : std::max(1u, components(a.type))));
                const auto values_offset = r.pos;
                if (a.type < 6)
                    r.skip(std::size_t(count) * stride(a.type));
                else
                    for (std::uint32_t j = 0; j < count; ++j)
                        scalar(r, a.type);
                auto elements = r.count(256);
                entries += elements;
                if (retain_ranges && a.type == 4)
                    r.budget(elements);
                std::vector<std::uint32_t> starts, counts;
                for (std::uint32_t j = 0; j < elements; ++j)
                    starts.push_back(r.count(count));
                for (std::uint32_t j = 0; j < elements; ++j)
                    counts.push_back(r.count(count));
                for (std::uint32_t j = 0; j < elements; ++j) {
                    auto capacity = r.count(count);
                    if (counts[j] > capacity || capacity > count - starts[j])
                        throw Error("mesh-attribute", "Unbounded attribute range escapes chunk", r.pos);
                    if (retain_ranges && a.type == 4)
                        channel.ranges.push_back({values_offset + std::size_t(starts[j]) * 4, counts[j]});
                }
            }
            auto elements = r.count(max_elements);
            if (elements != a.elements || entries != elements || chunks != (elements + 255) / 256)
                throw Error("mesh-attribute", "Unbounded attribute element mismatch", r.pos);
            scalar(r, a.type);
            channel.count = elements;
        }
        a.channels.push_back(channel);
        descriptions.push_back({{"values", channel.count}, {"sha256", r.hash(begin, r.pos)}});
    }
    auto defaults = scalar(r, a.type);
    auto flags = r.get<std::uint32_t>();
    a.metadata = {{"type", a.type},      {"extent", a.extent}, {"elements", a.elements},
                  {"default", defaults}, {"flags", flags},     {"channels", descriptions}};
    return a;
}
Elements read_elements(Reader &r, Json &metadata) {
    Elements result;
    auto kinds = r.count(64);
    for (std::uint32_t kind = 0; kind < kinds; ++kind) {
        auto name = r.string();
        if (result.count(name))
            throw Error("mesh-identity", "Duplicate mesh element kind", r.pos);
        auto count = r.count(32);
        std::vector<Container> channels;
        Json channel_info = Json::array();
        for (std::uint32_t c = 0; c < count; ++c) {
            Container container;
            auto bits = r.count(r.options.max_elements);
            container.allocated.reserve(bits);
            for (std::uint32_t word = 0; word < (bits + 31) / 32; ++word) {
                auto mask = r.get<std::uint32_t>();
                for (unsigned bit = 0; bit < 32 && container.allocated.size() < bits; ++bit) {
                    bool active = (mask & (1u << bit)) != 0;
                    container.allocated.push_back(active);
                    if (active)
                        ++container.active;
                }
            }
            auto holes = r.count(bits);
            if (holes != bits - container.active)
                throw Error("mesh-sparse", "Sparse mesh element count disagrees with bitset", r.pos);
            auto elements = r.count(r.options.max_elements);
            if (elements < bits)
                throw Error("mesh-attribute", "Mesh attributes are smaller than element index space", r.pos);
            auto attributes = r.count(256);
            Json attr_info = Json::object();
            for (std::uint32_t i = 0; i < attributes; ++i) {
                auto key = r.string();
                if (container.attributes.count(key))
                    throw Error("mesh-identity", "Duplicate mesh attribute name", r.pos);
                auto attribute = read_attribute(r, r.options.max_elements,
                                                key == "SkinWeights" || key.rfind("SkinWeights-", 0) == 0);
                if (attribute.elements != elements)
                    throw Error("mesh-attribute", "Attribute element count disagrees with container", r.pos);
                attr_info[key] = attribute.metadata;
                container.attributes.emplace(key, std::move(attribute));
            }
            channel_info.push_back(
                {{"indexSpace", bits}, {"active", container.active}, {"attributes", attr_info}});
            channels.push_back(std::move(container));
        }
        metadata[name] = channel_info;
        result.emplace(name, std::move(channels));
    }
    if (r.pos != r.size())
        throw Error("mesh-trailing", "Mesh description has unconsumed bytes", r.pos);
    return result;
}
const Container &primary(const Elements &elements, const char *name) {
    auto it = elements.find(name);
    if (it == elements.end() || it->second.size() != 1)
        throw Error("mesh-layout", std::string("Missing or ambiguous mesh element kind: ") + name);
    return it->second[0];
}
const Attribute *attribute(const Container &c, const char *name, std::uint32_t type, std::uint32_t extent,
                           bool required = true) {
    auto it = c.attributes.find(name);
    if (it == c.attributes.end()) {
        if (required)
            throw Error("mesh-layout", std::string("Missing mesh attribute: ") + name);
        return nullptr;
    }
    if (it->second.type != type || it->second.extent != extent || it->second.channels.empty())
        throw Error("mesh-layout", std::string("Unexpected attribute layout: ") + name);
    return &it->second;
}
void active(const Container &c, std::int32_t id) {
    if (id < 0 || static_cast<std::uint32_t>(id) >= c.allocated.size() || !c.allocated[id])
        throw Error("mesh-reference", "Mesh index references a missing element");
}
std::int32_t integer(Reader &r, const Attribute &a, std::uint32_t id, unsigned component = 0,
                     unsigned channel = 0) {
    r.pos = a.channels.at(channel).offset + (std::size_t(id) * a.extent + component) * 4;
    return r.get<std::int32_t>();
}
template <std::size_t N>
std::array<float, N> floats(Reader &r, const Attribute &a, std::uint32_t id, unsigned channel = 0) {
    std::array<float, N> result{};
    r.pos = a.channels.at(channel).offset + std::size_t(id) * N * 4;
    for (auto &f : result) {
        f = r.get<float>();
        if (!std::isfinite(f))
            throw Error("mesh-number", "Non-finite mesh geometry value", r.pos - 4);
    }
    return result;
}
} // namespace
bool is_mesh_description(const ObjectExport &object) {
    return object.class_name == "/Script/Engine.StaticMeshDescriptionBulkData" ||
           object.class_name == "/Script/Engine.SkeletalMeshDescriptionBulkData" ||
           object.class_name == "/Script/MeshDescription.MeshDescriptionBaseBulkData";
}
MeshSourceResult decode_mesh_source(Archive &a, const Package &package, std::size_t index, std::uint64_t end,
                                    const ReadOptions &options) {
    MeshSourceResult result;
    auto &source = result.source;
    source.object_path = package.exports[index].path;
    source.ue5_version = package.summary.ue5_version;
    source.custom_versions = package.summary.custom_versions;
    if (source.custom_versions.value("e4b068edf49442e9a231da0b2e46bb41", -1) != 40 ||
        (source.custom_versions.value("9c54d522a8264fbe9421074661b482d0", -1) < 44 ||
         source.custom_versions.value("9c54d522a8264fbe9421074661b482d0", -1) > 51))
        throw Error("mesh-profile", "Unsupported MeshDescription bulk version profile");
    Archive::ReadLimit window(a, end);
    result.native = {{"kind", "mesh-description-source"}};
    if (native_bool(a))
        result.native["objectGuid"] = a.guid();
    auto bulk = read_editor_bulk(a, package, options);
    source.payload = std::move(bulk.payload);
    result.native["bulkGuid"] = bulk.guid;
    result.native["bulkFlags"] = bulk.flags;
    result.native["guid"] = a.guid();
    auto enterprise = source.custom_versions.value("9dffbcd6494f0158e22112823c92a888", -1);
    if (enterprise > 10)
        throw Error("mesh-profile", "Unsupported enterprise bulk version");
    result.native["guidIsHash"] = enterprise >= 8 ? native_bool(a) : false;
    result.end = a.tell();
    if (source.payload.storage == "virtualized")
        source.diagnostics.push_back(
            "Virtualized mesh source requires an explicitly selected payload store.");
    else if (source.payload.storage != "local" && source.payload.storage != "empty")
        source.diagnostics.push_back("Mesh source is " + source.payload.storage +
                                     "; geometry is unavailable.");
    return result;
}
MeshGeometry decode_mesh_description(const std::vector<std::uint8_t> &bytes, const MeshSource &source,
                                     const MeshDecodeOptions &options) {
    auto main = source.custom_versions.value("697dd581e64f41abaa4a51ecbeb7b628", -1);
    if (main < 51 || main > 119 ||
        source.custom_versions.value("e4b068edf49442e9a231da0b2e46bb41", -1) != 40 ||
        (source.custom_versions.value("9c54d522a8264fbe9421074661b482d0", -1) < 44 ||
         source.custom_versions.value("9c54d522a8264fbe9421074661b482d0", -1) > 51))
        throw Error("mesh-profile", "Unsupported MeshDescription serialization profile");
    if (!options.max_elements || !options.max_attribute_values)
        throw Error("limit", "Mesh decode limits must be positive");
    options.control.checkpoint("Decoding mesh", 0, bytes.size());
    Reader r(bytes, options);
    r.wide_transform = source.ue5_version >= 1004;
    r.transform_version_known = source.ue5_version >= 1000 && source.ue5_version <= 1016;
    const auto fortnite = source.custom_versions.value("601d1886ac644f84aa16d3de0deac7d6", -1);
    r.legacy_transform = fortnite >= 0 && fortnite < 133;
    MeshGeometry mesh;
    auto elements = read_elements(r, mesh.attributes);
    const auto &vertices = primary(elements, "Vertices");
    const auto &instances = primary(elements, "VertexInstances");
    const auto &triangles = primary(elements, "Triangles");
    const auto &groups = primary(elements, "PolygonGroups");
    const auto &polygons = primary(elements, "Polygons");
    const Attribute *positions = attribute(vertices, "Position ", 1, 1, false);
    if (!positions)
        positions = attribute(vertices, "Position", 1, 1);
    auto vertex = attribute(instances, "VertexIndex", 4, 1);
    auto uv = attribute(instances, "TextureCoordinate", 2, 1, false);
    auto normal = attribute(instances, "Normal", 1, 1, false);
    auto tangent = attribute(instances, "Tangent", 1, 1, false);
    auto color = attribute(instances, "Color", 0, 1, false);
    auto sign = attribute(instances, "BinormalSign", 3, 1, false);
    auto corners = attribute(triangles, "VertexInstanceIndex", 4, 3);
    auto group = attribute(triangles, "PolygonGroupIndex", 4, 1);
    auto polygon = attribute(triangles, "PolygonIndex", 4, 1);
    auto slots = attribute(groups, "ImportedMaterialSlotName", 6, 1, false);
    for (std::uint32_t id = 0; id < vertices.allocated.size(); ++id)
        if (vertices.allocated[id]) {
            auto p = floats<3>(r, *positions, id);
            mesh.vertices.push_back({id, p});
        }
    for (std::uint32_t id = 0; id < instances.allocated.size(); ++id)
        if (instances.allocated[id]) {
            MeshInstance item;
            item.id = id;
            auto v = integer(r, *vertex, id);
            active(vertices, v);
            item.vertex = static_cast<std::uint32_t>(v);
            if (uv)
                for (unsigned c = 0; c < uv->channels.size(); ++c)
                    item.uvs.push_back(floats<2>(r, *uv, id, c));
            if (normal)
                item.normal = floats<3>(r, *normal, id);
            if (tangent)
                item.tangent = floats<3>(r, *tangent, id);
            if (color)
                item.color = floats<4>(r, *color, id);
            if (sign)
                item.binormal_sign = floats<1>(r, *sign, id)[0];
            mesh.instances.push_back(std::move(item));
        }
    for (std::uint32_t id = 0; id < triangles.allocated.size(); ++id)
        if (triangles.allocated[id]) {
            MeshTriangle item;
            item.id = id;
            for (unsigned c = 0; c < 3; ++c) {
                auto vi = integer(r, *corners, id, c);
                active(instances, vi);
                item.instances[c] = static_cast<std::uint32_t>(vi);
            }
            auto g = integer(r, *group, id);
            auto p = integer(r, *polygon, id);
            active(groups, g);
            active(polygons, p);
            item.group = static_cast<std::uint32_t>(g);
            item.polygon = static_cast<std::uint32_t>(p);
            mesh.triangles.push_back(item);
        }
    if (slots) {
        r.pos = slots->channels[0].offset;
        for (std::uint32_t id = 0; id < slots->elements; ++id) {
            auto name = r.string();
            if (id < groups.allocated.size() && groups.allocated[id])
                mesh.material_slots[id] = std::move(name);
        }
    }
    for (std::uint32_t id = 0; id < groups.allocated.size(); ++id)
        if (groups.allocated[id] && !mesh.material_slots.count(id))
            mesh.material_slots[id] = "None";
    if (!mesh.vertices.empty()) {
        auto min = mesh.vertices[0].position, max = min;
        for (const auto &v : mesh.vertices)
            for (unsigned c = 0; c < 3; ++c) {
                min[c] = std::min(min[c], v.position[c]);
                max[c] = std::max(max[c], v.position[c]);
            }
        mesh.bounds = {{"min", min}, {"max", max}};
    }
    if (elements.count("BonesElementName") ||
        std::any_of(vertices.attributes.begin(), vertices.attributes.end(), [](const auto &item) {
            return item.first == "SkinWeights" || item.first.rfind("SkinWeights-", 0) == 0;
        })) {
        auto skin = std::make_shared<MeshSkin>();
        const auto &bones = primary(elements, "BonesElementName");
        const auto *names = attribute(bones, "Name", 6, 1);
        const auto *parents = attribute(bones, "ParentIndex", 4, 1);
        const auto *poses = attribute(bones, "Pose", 7, 1);
        if (names->channels.size() != 1 || parents->channels.size() != 1 || poses->channels.size() != 1)
            throw Error("mesh-skin-layout", "Source bone attributes require a single channel");
        r.pos = names->channels[0].offset;
        std::map<std::uint32_t, std::string> bone_names;
        for (std::uint32_t id = 0; id < names->elements; ++id) {
            auto name = r.string();
            if (id < bones.allocated.size() && bones.allocated[id])
                bone_names[id] = std::move(name);
        }
        for (const auto &item : bone_names) {
            options.control.check();
            MeshBone bone;
            bone.id = item.first;
            bone.name = item.second;
            bone.parent = integer(r, *parents, bone.id);
            r.pos = poses->channels[0].offset + std::size_t(bone.id) * (r.wide_transform ? 80 : 40);
            const auto pose = scalar(r, 7);
            bone.rotation = pose["rotation"].get<std::array<double, 4>>();
            bone.translation = pose["translation"].get<std::array<double, 3>>();
            bone.scale = pose["scale"].get<std::array<double, 3>>();
            skin->bones.push_back(std::move(bone));
        }
        std::set<std::string> profile_keys;
        for (const auto &item : vertices.attributes) {
            if (item.first != "SkinWeights" && item.first.rfind("SkinWeights-", 0) != 0)
                continue;
            const auto profile = item.first == "SkinWeights" ? std::string("Default") : item.first.substr(12);
            std::string key = profile;
            for (auto &c : key)
                if (c >= 'A' && c <= 'Z')
                    c += 'a' - 'A';
            if (key.empty() || (item.first != "SkinWeights" && (key == "none" || key == "default")) ||
                !profile_keys.insert(key).second)
                throw Error("mesh-skin-profile", "Empty or duplicate source weight profile");
            const auto *weights = attribute(vertices, item.first.c_str(), 4, 0);
            if (weights->channels.size() != 1 || weights->channels[0].ranges.size() != weights->elements)
                throw Error("mesh-skin-layout", "Skin weights require one unbounded channel");
            auto &result = skin->profiles[profile];
            for (const auto &source_vertex : mesh.vertices) {
                options.control.check();
                const auto &range = weights->channels[0].ranges.at(source_vertex.id);
                r.pos = range.offset;
                auto &influences = result[source_vertex.id];
                for (std::uint32_t i = 0; i < range.count; ++i) {
                    const auto packed = r.get<std::uint32_t>();
                    influences.push_back({packed >> 16, static_cast<std::uint16_t>(packed & 65535)});
                }
            }
        }
        skin->validate(options.control);
        mesh.skin = std::move(skin);
    }
    auto morphs = std::make_shared<MeshMorphs>();
    auto folded = [](std::string name) {
        for (auto &c : name)
            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
        return name;
    };
    std::map<std::string, std::string> morph_names;
    for (const auto &item : vertices.attributes) {
        if (folded(item.first).rfind("morph-", 0) != 0)
            continue;
        const auto name = item.first.substr(6), key = folded(name);
        if (key.empty() || key == "none" || name.find('\0') != std::string::npos ||
            !morph_names.emplace(key, name).second)
            throw Error("mesh-morph-identity", "Empty or ambiguous source morph target name");
        const auto *deltas = attribute(vertices, item.first.c_str(), 1, 1);
        if (deltas->channels.size() != 1)
            throw Error("mesh-morph-layout", "Morph deltas require one vector channel");
        auto &target = morphs->targets[name];
        for (const auto &v : mesh.vertices)
            target.positions.emplace(v.id, floats<3>(r, *deltas, v.id));
    }
    for (const auto &item : instances.attributes) {
        if (folded(item.first).rfind("morph-", 0) != 0)
            continue;
        const auto found = morph_names.find(folded(item.first.substr(6)));
        if (found == morph_names.end())
            throw Error("mesh-morph-reference", "Morph normals have no position attribute");
        auto &target = morphs->targets.at(found->second);
        if (target.normals_present)
            throw Error("mesh-morph-identity", "Duplicate source morph normal attribute");
        const auto *deltas = attribute(instances, item.first.c_str(), 1, 1);
        if (deltas->channels.size() != 1)
            throw Error("mesh-morph-layout", "Morph deltas require one vector channel");
        target.normals_present = true;
        for (const auto &i : mesh.instances)
            target.normals.emplace(i.id, floats<3>(r, *deltas, i.id));
    }
    if (!morphs->targets.empty()) {
        morphs->validate(mesh, options.control);
        mesh.morphs = std::move(morphs);
    }
    return mesh;
}
} // namespace cyuediff::detail
namespace cyuediff {
Json MeshSource::to_json(bool location) const {
    TextureSource wrapper;
    wrapper.payload = payload;
    return {{"path", object_path},
            {"payload", wrapper.to_json(location).at("payload")},
            {"ue5Version", ue5_version},
            {"customVersions", custom_versions},
            {"diagnostics", diagnostics}};
}
MeshGeometry decode_mesh(const MeshSource &source, const MeshDecodeOptions &options) {
    return detail::decode_mesh_description(decode_bulk_payload(source.payload, options), source, options);
}
Json MeshGeometry::to_json() const {
    Json v = Json::object(), vi = Json::object(), t = Json::object(), groups = Json::object();
    for (const auto &item : vertices)
        v[std::to_string(item.id)] = {{"position", item.position}};
    for (const auto &item : instances)
        vi[std::to_string(item.id)] = {{"vertex", item.vertex},
                                       {"uvs", item.uvs},
                                       {"normal", item.normal},
                                       {"tangent", item.tangent},
                                       {"binormalSign", item.binormal_sign},
                                       {"color", item.color}};
    for (const auto &item : triangles)
        t[std::to_string(item.id)] = {
            {"instances", item.instances}, {"polygon", item.polygon}, {"group", item.group}};
    for (const auto &item : material_slots)
        groups[std::to_string(item.first)] = item.second;
    Json result = {
        {"schema", "cyuediff.geometry/1"}, {"vertices", v},    {"instances", vi},         {"triangles", t},
        {"materialSlots", groups},         {"bounds", bounds}, {"attributes", attributes}};
    if (skin)
        result["skin"] = skin->to_json();
    if (morphs) {
        morphs->validate(*this);
        result["morphs"] = morphs->to_json();
    }
    if (vertex_attributes && !vertex_attributes->channels.empty())
        result["vertexAttributes"] = vertex_attributes->semantic_snapshot();
    if (vertex_overlaps)
        result["vertexOverlaps"] = vertex_overlaps->semantic_snapshot();
    if (cloth)
        result["cloth"] = cloth->semantic_snapshot();
    if (render_morphs)
        result["renderMorphs"] = render_morphs->semantic_snapshot();
    return result;
}
Json MeshComparison::to_json() const {
    Json out = {{"schema", "cyuediff.mesh-diff/1"},
                {"status", !complete         ? "incomplete"
                           : changes.empty() ? "identical"
                                             : "changed"},
                {"changes", changes}};
    if (!complete) {
        out["complete"] = false;
        out["diagnostics"] = diagnostics;
    }
    return out;
}
const MeshSource &Package::mesh_source(const std::string &object, std::uint32_t lod) const {
    if (!object.empty())
        for (const auto &s : mesh_sources)
            if (s.object_path == object)
                return s;
    const ObjectExport *selected = nullptr;
    for (const auto &e : exports)
        if ((e.class_name == "/Script/Engine.StaticMesh" || e.class_name == "/Script/Engine.SkeletalMesh") &&
            (object.empty() || e.path == object || e.name == object)) {
            if (selected)
                throw Error("mesh-selection", "Several meshes match; select one with --object");
            selected = &e;
        }
    if (!selected) {
        if (object.empty() && lod == 0 && mesh_sources.size() == 1)
            return mesh_sources.front();
        throw Error("mesh-selection", "No matching source mesh in package");
    }
    auto models = detail::value(selected->properties, "SourceModels");
    if (!models || !models->is_array() || lod >= models->size())
        throw Error("mesh-lod", "Requested source LOD does not exist");
    auto ref = detail::value(models->at(lod), selected->class_name == "/Script/Engine.SkeletalMesh"
                                                  ? "MeshDescriptionBulkData"
                                                  : "StaticMeshDescriptionBulkData");
    if (!ref || !ref->is_string())
        throw Error("mesh-source", "Source LOD has no MeshDescription bulk object");
    for (const auto &s : mesh_sources)
        if (s.object_path == ref->get<std::string>())
            return s;
    throw Error("mesh-source", "Referenced mesh source is unavailable");
}
Json Package::mesh_snapshot() const {
    Json assets = Json::object(), sources = Json::object();
    for (const auto &s : mesh_sources)
        sources[s.object_path] = s.to_json();
    for (const auto &e : exports)
        if (e.class_name == "/Script/Engine.StaticMesh" || e.class_name == "/Script/Engine.SkeletalMesh") {
            Json settings = e.properties;
            for (const auto *key : {"SourceModels", "StaticMaterials", "HiResSourceModel", "AssetImportData"})
                settings.erase(key);
            Json entry = {{"settings", settings}, {"lods", Json::array()}, {"materials", Json::array()}};
            if (auto materials = detail::value(e.properties, "StaticMaterials"))
                entry["materials"] = *materials;
            if (auto models = detail::value(e.properties, "SourceModels"))
                entry["lods"] = *models;
            if (auto hi = detail::value(e.properties, "HiResSourceModel"))
                entry["hiRes"] = *hi;
            if (e.class_name == "/Script/Engine.SkeletalMesh")
                entry["kind"] = "skeletal";
            assets[e.path] = std::move(entry);
        }
    return {{"schema", "cyuediff.meshes/1"}, {"meshes", assets}, {"sources", sources}};
}
void write_obj(const fs::path &path, const MeshGeometry &mesh) {
    std::map<std::uint32_t, const MeshVertex *> vertices;
    std::map<std::uint32_t, std::size_t> vertex_indices;
    for (const auto &v : mesh.vertices) {
        if (!vertices.emplace(v.id, &v).second)
            throw Error("mesh-identity", "Duplicate vertex ID");
        for (auto component : v.position)
            if (!std::isfinite(component))
                throw Error("mesh-number", "Invalid OBJ vertex");
        vertex_indices[v.id] = vertex_indices.size() + 1;
    }
    std::map<std::uint32_t, std::size_t> instances;
    for (std::size_t i = 0; i < mesh.instances.size(); ++i) {
        const auto &v = mesh.instances[i];
        if (!instances.emplace(v.id, i + 1).second || !vertices.count(v.vertex))
            throw Error("mesh-reference", "Invalid OBJ vertex instance");
    }
    for (const auto &t : mesh.triangles)
        for (auto id : t.instances)
            if (!instances.count(id))
                throw Error("mesh-reference", "OBJ triangle references missing instance");
    // Written beside the destination, then replaced: a failure never leaves a truncated OBJ.
    detail::atomic_output(path, [&](std::ostream &out) {
        out.imbue(std::locale::classic());
        out << std::setprecision(std::numeric_limits<float>::max_digits10)
            << "# CyUEDiff source coordinates; build settings and coordinate conversions are not applied\n";
        for (const auto &vertex : mesh.vertices) {
            const auto &p = vertex.position;
            out << "v " << p[0] << ' ' << p[1] << ' ' << p[2] << '\n';
        }
        for (const auto &i : mesh.instances) {
            auto uv = i.uvs.empty() ? std::array<float, 2>{} : i.uvs[0];
            out << "vt " << uv[0] << ' ' << uv[1] << '\n';
        }
        for (const auto &i : mesh.instances)
            out << "vn " << i.normal[0] << ' ' << i.normal[1] << ' ' << i.normal[2] << '\n';
        for (const auto &t : mesh.triangles) {
            out << "g section_" << t.group << "\nf";
            for (auto id : t.instances) {
                auto index = instances.at(id);
                out << ' ' << vertex_indices.at(mesh.instances.at(index - 1).vertex) << '/' << index << '/'
                    << index;
            }
            out << '\n';
        }
        if (!out)
            throw Error("io", "Cannot write mesh OBJ");
    });
}
} // namespace cyuediff
