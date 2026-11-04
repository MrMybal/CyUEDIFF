#include "../src/native.hpp"
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace cyuediff;
namespace {
int checks = 0;
void check(bool good, const char *message) {
    ++checks;
    if (!good)
        throw std::runtime_error(message);
}
struct Bytes {
    std::vector<unsigned char> data;
    std::size_t direction = 0, friendly_history = 0;
    template <class T> void put(T value) {
        auto p = reinterpret_cast<unsigned char *>(&value);
        data.insert(data.end(), p, p + sizeof(T));
    }
    void i(int value) {
        put<std::int32_t>(value);
    }
    void guid(unsigned value) {
        put<std::uint32_t>(value);
        for (int i = 0; i < 3; ++i)
            put<std::uint32_t>(0);
    }
    void name(int value) {
        i(value);
        i(0);
    }
    void string(const std::string &value) {
        i(static_cast<int>(value.size() + 1));
        data.insert(data.end(), value.begin(), value.end());
        put<char>(0);
    }
    void null_ref() {
        i(1);
    }
    void ref(int node, unsigned pin) {
        i(0);
        i(node);
        guid(pin);
    }
    void empty_text() {
        i(0);
        put<std::int8_t>(-1);
        i(0);
    }
    void save(const fs::path &path) const {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    void patch(std::size_t at, int value) {
        std::memcpy(data.data() + at, &value, 4);
    }
};
std::string guid(unsigned value) {
    std::ostringstream s;
    s << std::hex << std::setw(8) << std::setfill('0') << value << std::string(24, '0');
    return s.str();
}
Package package() {
    Package p;
    p.summary.ue4_version = 522;
    p.summary.ue5_version = 1013;
    p.summary.custom_versions = {
        {"cffc743f43b04480939114df171d2073", 37}, {"b0d832e41f894f0daccf7eb736fd4aa2", 10},
        {"9c54d522a8264fbe9421074661b482d0", 44}, {"697dd581e64f41abaa4a51ecbeb7b628", 119},
        {"d89b5e4224bd4d468412aca8df641779", 51}, {"e4b068edf49442e9a231da0b2e46bb41", 40}};
    p.names = {"None", "execute", "exec", "int"};
    ObjectExport graph;
    graph.name = graph.path = "Graph";
    graph.class_name = "/Script.Engine.EdGraph";
    graph.properties = {{"GraphGuid", {{"value", guid(100)}}},
                        {"Nodes", {{"value", Json::array({"Graph.A", "Graph.B"})}}}};
    p.exports.push_back(graph);
    for (int i = 0; i < 2; ++i) {
        ObjectExport node;
        node.name = i ? "B" : "A";
        node.path = "Graph." + node.name;
        node.outer_index = 1;
        node.class_name = "/Script.BlueprintGraph.K2Node_CallFunction";
        node.properties = {{"NodeGuid", {{"value", guid(i ? 20 : 10)}}}, {"NodePosX", {{"value", i * 256}}}};
        p.exports.push_back(node);
    }
    return p;
}
Bytes payload(int owner, unsigned pin, int peer, unsigned peer_pin, bool map = false) {
    Bytes b;
    b.i(0);
    b.i(1);
    b.ref(owner, pin);
    b.i(owner);
    b.guid(pin);
    b.name(1);
    b.i(0);
    b.friendly_history = b.data.size();
    b.put<std::int8_t>(0);
    b.string("Tests");
    b.string("FriendlyName");
    b.string("Execute");
    b.i(-1);
    b.string("Pin tooltip");
    b.direction = b.data.size();
    b.put<std::uint8_t>(owner == 2 ? 1 : 0);
    b.name(2);
    b.name(0);
    b.i(0);
    b.put<std::uint8_t>(map ? 3 : 0);
    if (map) {
        b.name(3);
        b.name(0);
        b.i(0);
        b.i(0);
        b.i(0);
        b.i(0);
    }
    b.i(0);
    b.i(0);
    b.i(0);
    b.name(0);
    b.guid(0);
    b.i(0);
    b.i(0);
    b.i(0);
    b.string("7");
    b.string("0");
    b.i(0);
    b.empty_text();
    b.i(1);
    b.ref(peer, peer_pin);
    b.i(0);
    b.null_ref();
    b.null_ref();
    b.guid(pin);
    b.i(0);
    return b;
}
template <class Function> void error(Function fn, const char *expected) {
    try {
        fn();
        throw std::runtime_error(std::string("Expected error ") + expected);
    } catch (const Error &e) {
        check(e.code == expected, ("Unexpected error " + e.code).c_str());
    }
}
} // namespace
int main() {
    auto root =
        fs::current_path() / (".test-tmp-blueprint-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        auto p = package();
        auto a = payload(2, 1, 3, 2), b = payload(3, 2, 2, 1);
        ReadOptions options;
        auto decode = [&](const Bytes &bytes, std::size_t owner, std::uint64_t end = 0) {
            auto file = root / "native.bin";
            bytes.save(file);
            detail::Archive archive(file, options);
            return detail::decode_edgraph_node(archive, p, owner, end ? end : bytes.data.size(), options);
        };
        auto decoded = decode(a, 1);
        check(decoded.end == a.data.size(), "Exact native extent");
        check(decoded.data.at("pins").at(guid(1)).at("name") == "execute", "Pin name");
        check(decoded.data.at("pins").at(guid(1)).at("friendlyName").at("source") == "Execute",
              "Localized pin text");
        check(decoded.data.at("pins").at(guid(1)).at("linkedTo")[0].at("node") == "Graph.B",
              "Resolved pin owner path");
        p.exports[1].native_data = decoded.data;
        p.exports[1].native_coverage = "pins";
        p.exports[2].native_data = decode(b, 2).data;
        p.exports[2].native_coverage = "pins";
        auto model = p.blueprint_snapshot();
        const auto &graph = model.at("graphs").at(guid(100));
        check(graph.at("complete") == true, "Complete graph");
        check(graph.at("nodes").size() == 2, "Two nodes");
        check(graph.at("links").size() == 1, "Reciprocal references become one edge");
        const auto &edge = graph.at("links").begin().value();
        check(edge.at("from").at("node") == guid(10) && edge.at("to").at("node") == guid(20),
              "Output-to-input direction");
        auto removed = p;
        for (auto &object : removed.exports)
            if (object.native_coverage == "pins")
                for (auto &pin : object.native_data["pins"])
                    pin["linkedTo"] = Json::array();
        auto diff = compare(p, removed);
        check(diff.graph_changes.size() == 1, "One graph change on disconnect");
        check(diff.graph_changes[0].at("op") == "remove", "Connection removal");
        check(diff.graph_changes[0].at("path").get<std::string>().find("/links/") != std::string::npos,
              "Connection diff path");
        auto changed = p;
        changed.exports[1].native_data["pins"][guid(1)]["defaultValue"] = "42";
        auto defaults = compare(p, changed);
        check(defaults.graph_changes.size() == 1, "One default change");
        check(defaults.graph_changes[0].at("path") ==
                  "/graphs/" + guid(100) + "/nodes/" + guid(10) + "/pins/" + guid(1) + "/defaultValue",
              "Stable pin GUID diff");
        auto renamed = p;
        renamed.exports[1].name = "Renamed";
        renamed.exports[1].path = "Graph.Renamed";
        renamed.exports[0].properties["Nodes"]["value"][0] = "Graph.Renamed";
        renamed.exports[2].native_data["pins"][guid(2)]["linkedTo"][0]["node"] = "Graph.Renamed";
        auto renamed_graph = renamed.blueprint_snapshot().at("graphs").at(guid(100));
        check(renamed_graph.at("nodes").contains(guid(10)), "Node GUID survives object rename");
        check(renamed_graph.at("links") == graph.at("links"), "Connections survive object rename");
        auto dangling = p;
        dangling.exports[1].native_data["pins"][guid(1)]["linkedTo"][0]["pin"] = guid(99);
        auto dangling_graph = dangling.blueprint_snapshot().at("graphs").at(guid(100));
        check(dangling_graph.at("complete") == false && !dangling_graph.at("links").empty(),
              "Dangling connection retained");
        auto orphan = p;
        orphan.exports[0].properties["Nodes"]["value"] = Json::array({"Graph.A"});
        check(orphan.blueprint_snapshot().at("graphs").at(guid(100)).at("complete") == false,
              "Orphan export detected");
        auto duplicate = p;
        duplicate.exports[2].properties["NodeGuid"]["value"] = guid(10);
        auto duplicate_graph = duplicate.blueprint_snapshot().at("graphs").at(guid(100));
        check(duplicate_graph.at("complete") == false && duplicate_graph.at("nodes").size() == 2,
              "Duplicate node IDs retain both objects");
        auto ordinary = p;
        ObjectExport data;
        data.name = data.path = "Data";
        data.class_name = "DataAsset";
        data.properties = {{"GraphGuid", {{"value", guid(200)}}}};
        ordinary.exports.push_back(data);
        check(ordinary.blueprint_snapshot().at("graphs").size() == 1,
              "Ordinary GraphGuid property is not a Blueprint graph");
        auto map = decode(payload(2, 1, 3, 2, true), 1);
        check(map.data.at("pins").at(guid(1)).at("type").at("valueType").at("category") == "int",
              "Map terminal type");
        auto bad = a;
        bad.patch(0, 2);
        error([&] { decode(bad, 1); }, "pin-boolean");
        bad = a;
        bad.data[bad.direction] = 2;
        error([&] { decode(bad, 1); }, "pin-direction");
        bad = a;
        bad.data[bad.friendly_history] = 4;
        error([&] { decode(bad, 1); }, "pin-text-history");
        bad = a;
        bad.patch(12, 3);
        error([&] { decode(bad, 1); }, "pin-owner");
        bad = a;
        bad.patch(4, -1);
        error([&] { decode(bad, 1); }, "limit");
        bad = a;
        bad.patch(4, 2);
        bad.data.insert(bad.data.end(), a.data.begin() + 8, a.data.end());
        error([&] { decode(bad, 1); }, "pin-id");
        error([&] { decode(a, 1, a.data.size() - 1); }, "bounds");
        p.summary.custom_versions["697dd581e64f41abaa4a51ecbeb7b628"] = 120;
        error([&] { decode(a, 1); }, "pin-profile");
        p.summary.custom_versions["697dd581e64f41abaa4a51ecbeb7b628"] = 119;
        {
            auto archive_file = root / "range.bin";
            a.save(archive_file);
            detail::Archive archive(archive_file, options);
            try {
                detail::Archive::ReadLimit limit(archive, 4);
                archive.skip(5);
                check(false, "Window expected to reject");
            } catch (const Error &e) {
                check(e.code == "bounds", "Window rejects neighboring bytes");
            }
            archive.seek(a.data.size());
            check(archive.tell() == a.data.size(), "Window restores after failure");
        }
        fs::remove_all(root);
        std::cout << checks << " Blueprint checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAILED: " << e.what() << "; fixtures: " << root << '\n';
        return 1;
    }
}
