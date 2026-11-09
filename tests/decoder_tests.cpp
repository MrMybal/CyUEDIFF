#include <cyuediff/decoders.hpp>
#include "package_fixture.hpp"
#include <chrono>
#include <iostream>
#include <limits>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
int checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void error(F f, const char *code) {
    try {
        f();
        throw std::runtime_error("Expected error");
    } catch (const Error &e) {
        check(e.code == code, "Expected error code");
    }
}
ReadOptions with(std::vector<AssetDecoder> entries) {
    ReadOptions options;
    options.decoders = std::make_shared<const DecoderRegistry>(std::move(entries));
    return options;
}
AssetDecoder native(DecoderCallback callback, std::string id = "test.native/1") {
    AssetDecoder decoder;
    decoder.id = std::move(id);
    decoder.kind = DecoderKind::native_export;
    decoder.target = "/Script/CoreUObject.DataAsset";
    decoder.decode = std::move(callback);
    return decoder;
}
AssetDecoder structure(DecoderCallback callback) {
    auto decoder = native(std::move(callback), "test.record/1");
    decoder.kind = DecoderKind::struct_value;
    decoder.target = "ExampleStruct";
    return decoder;
}
Json read_record(DecoderInput &input, const DecoderContext &context) {
    auto number = input.get<std::int32_t>();
    auto name = input.name(), text = input.string(), reference = input.reference(), guid = input.guid();
    return {{"number", number}, {"name", name}, {"text", text}, {"reference", reference}, {"guid", guid}};
}
// Controlled host-defined native struct, deliberately not a claim about an Unreal built-in format.
Fixture custom_fixture(int version, bool array = false, int value = 42) {
    auto f = fixture(version);
    Bytes script;
    auto name = [](Bytes &b, int id) {
        b.i(id);
        b.i(0);
    };
    if (version >= 1011)
        script.put<std::uint8_t>(0);
    auto tag = [&](int property, int type, int bytes) {
        name(script, property);
        name(script, type);
        if (version >= 1012) {
            script.i(1);
            if (type == 12) {
                name(script, 13);
                script.i(1);
            }
            name(script, 14);
            script.i(0);
        }
        script.i(bytes);
        if (version >= 1012)
            script.put<std::uint8_t>(0);
        else {
            script.i(0);
            name(script, type == 12 ? 13 : 14);
            if (type == 13)
                script.zeros(16);
            script.put<std::uint8_t>(0);
            if (version >= 1011)
                script.put<std::uint8_t>(0);
        }
    };
    Bytes payload;
    if (array) {
        payload.i(2);
        if (version < 1012) {
            name(payload, 11);
            name(payload, 13);
            payload.i(0);
            payload.i(0);
            name(payload, 14);
            payload.zeros(16);
            payload.put<std::uint8_t>(0);
            if (version >= 1011)
                payload.put<std::uint8_t>(0);
        }
    }
    for (int i = 0; i < (array ? 2 : 1); ++i) {
        payload.i(value + i);
        name(payload, 9);
        payload.string("plugin");
        payload.i(-1);
        payload.i(1);
        payload.i(2);
        payload.i(3);
        payload.i(4);
    }
    tag(11, array ? 12 : 13, static_cast<int>(payload.data.size()));
    script.append(payload);
    name(script, 5);
    name(script, 6);
    if (version >= 1012)
        script.i(0);
    script.i(4);
    if (version >= 1012)
        script.put<std::uint8_t>(0);
    else {
        script.i(0);
        script.put<std::uint8_t>(0);
        if (version >= 1011)
            script.put<std::uint8_t>(0);
    }
    script.i(99);
    name(script, 0);
    if (version >= 1010)
        f.header.patch(f.header.data.size() - 8, static_cast<int>(script.data.size()));
    script.i(9);
    f.header.patch(f.export_offset + 28, static_cast<int>(script.data.size()));
    f.script = std::move(script);
    return f;
}
} // namespace
int main(int argc, char **argv) {
    auto root =
        fs::current_path() /
        (".test-tmp-decoders-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        auto read_native = [](DecoderInput &input, const DecoderContext &context) -> Json {
            if (!context.exact_size || input.position() != 0 || input.remaining() != 4)
                throw Error("test", "Wrong native input window");
            return {{"counter", input.get<std::int32_t>()}};
        };
        auto decoder = native(read_native);
        check(DecoderRegistry({decoder}).size() == 1, "Registry size");
        auto invalid = decoder;
        invalid.id.clear();
        error([&] { DecoderRegistry registry({invalid}); }, "decoder-registration");
        invalid = decoder;
        invalid.target.clear();
        error([&] { DecoderRegistry registry({invalid}); }, "decoder-registration");
        invalid = decoder;
        invalid.decode = {};
        error([&] { DecoderRegistry registry({invalid}); }, "decoder-registration");
        error([&] { DecoderRegistry registry({decoder, decoder}); }, "decoder-registration");
        invalid = decoder;
        invalid.profile.ue5 = {1012, 1000};
        error([&] { DecoderRegistry registry({invalid}); }, "decoder-registration");
        invalid = decoder;
        invalid.profile.custom_versions["not-a-guid"] = {};
        error([&] { DecoderRegistry registry({invalid}); }, "decoder-registration");
        invalid = decoder;
        invalid.profile.custom_versions[std::string(32, 'a')] = {5, 1};
        error([&] { DecoderRegistry registry({invalid}); }, "decoder-registration");
        invalid = decoder;
        invalid.id = "id with space";
        error([&] { DecoderRegistry registry({invalid}); }, "decoder-registration");
        invalid = decoder;
        invalid.kind = static_cast<DecoderKind>(9);
        error([&] { DecoderRegistry registry({invalid}); }, "decoder-registration");
        SerializationProfile profile;
        profile.ue4 = {522, 522};
        profile.ue5 = {1012, 1018};
        profile.custom_versions[std::string(32, 'a')] = {2, 4};
        PackageSummary summary;
        summary.ue4_version = 522;
        summary.ue5_version = 1012;
        check(!profile.matches(summary), "Required GUID cannot be absent");
        summary.custom_versions[std::string(32, 'a')] = 2;
        check(profile.matches(summary), "Inclusive profile minimum");
        summary.ue5_version = 1018;
        summary.custom_versions[std::string(32, 'a')] = 4;
        check(profile.matches(summary), "Inclusive profile maximum");
        summary.custom_versions[std::string(32, 'a')] = 5;
        check(!profile.matches(summary), "Custom version mismatch");
        summary.custom_versions[std::string(32, 'a')] = 3;
        summary.ue5_version = 1011;
        check(!profile.matches(summary), "File version mismatch");
        summary.ue5_version = 1012;
        summary.ue4_version = 521;
        check(!profile.matches(summary), "UE4 version mismatch");
        for (int version : {0, 1009, 1011, 1012, 1016, 1018}) {
            auto file = save(root, "native" + std::to_string(version), fixture(version, 1, false, 9),
                             version == 1018);
            auto plain = Package::open(file);
            auto empty = Package::open(file, with({}));
            check(plain.to_json() == empty.to_json(), "Empty registry preserves contract");
            auto p = Package::open(file, with({decoder}));
            check(p.exports[0].native_coverage == "extension" &&
                      p.exports[0].native_data["data"]["counter"] == 9,
                  "Native suffix decoded across file versions");
            check(p.exports[0].opaque.empty() && p.exports[0].native_end - p.exports[0].native_start == 4,
                  "Exact native range consumed");
            for (bool array : {false, true}) {
                bool observed = false;
                auto record = structure([&](DecoderInput &input, const DecoderContext &context) {
                    observed = context.exact_size == !array && context.object.path == "TestAsset";
                    return read_record(input, context);
                });
                auto custom = save(root, "custom" + std::to_string(version) + (array ? "a" : "s"),
                                   custom_fixture(version, array), version == 1012);
                auto q = Package::open(custom, with({record, decoder}));
                auto entry = q.exports[0].properties.at("Items");
                auto result = array ? entry.at("value")[1] : entry.at("value");
                check(observed && result.at("decoder") == "test.record/1" &&
                          result.at("data").at("number") == (array ? 43 : 42),
                      "Struct callback and array context");
                check(result.at("data").at("name") == "Text" && result.at("data").at("text") == "plugin" &&
                          result.at("data").at("reference") == "/Script/CoreUObject.DataAsset" &&
                          result.at("data").at("guid") == "00000001000000020000000300000004",
                      "Unreal names strings references GUIDs");
                check(q.exports[0].properties.at("Value").at("value") == 99 &&
                          q.exports[0].coverage == "properties",
                      "Following property preserved");
            }
        }
        auto file = save(root, "base", fixture(1012, 1, false, 9));
        auto plain = Package::open(file);
        auto before = Package::open(file, with({decoder}));
        auto after = Package::open(save(root, "changed", fixture(1012, 1, false, 10)), with({decoder}));
        auto diff = compare(before, after);
        check(diff.status() == "changed" && diff.changes.size() == 1 &&
                  diff.changes[0]["path"] == "/objects/TestAsset/native/data/counter",
              "Decoded native diff");
        auto record = structure(read_record);
        auto a = Package::open(save(root, "record-before", custom_fixture(1012)), with({record}));
        auto b = Package::open(save(root, "record-after", custom_fixture(1012, false, 44)), with({record}));
        diff = compare(a, b);
        check(diff.changes.size() == 1 &&
                  diff.changes[0]["path"] == "/objects/TestAsset/properties/Items/value/data/number",
              "Decoded struct diff");
        auto other_id = decoder;
        other_id.id = "test.native/2";
        const auto different_format = compare(before, Package::open(file, with({other_id})));
        check(different_format.changes.empty() && different_format.status() == "unverified" &&
                  different_format.unavailable_scopes.size() == 1 &&
                  different_format.unavailable_scopes[0]["beforeDecoder"] == decoder.id &&
                  different_format.unavailable_scopes[0]["afterDecoder"] == other_id.id,
              "Different decoder identities retain context evidence without asserting stored field changes");
        auto mismatched = decoder;
        mismatched.profile.ue5 = {1016, 1018};
        check(Package::open(file, with({mismatched})).to_json() == plain.to_json(),
              "Version mismatch does not invoke callback");
        mismatched = decoder;
        mismatched.profile.custom_versions[std::string(32, 'a')] = {0, 9};
        check(Package::open(file, with({mismatched})).to_json() == plain.to_json(),
              "Missing custom version does not invoke callback");
        mismatched = decoder;
        mismatched.target = "DataAsset";
        check(Package::open(file, with({mismatched})).to_json() == plain.to_json(),
              "Exact class path required");
        auto ambiguous = Package::open(file, with({decoder, other_id}));
        check(ambiguous.exports[0].native_coverage == "opaque" &&
                  ambiguous.exports[0].opaque == plain.exports[0].opaque,
              "Ambiguous native profile opaque");
        auto options = with({decoder});
        options.properties = false;
        auto metadata = Package::open(file, options);
        check(metadata.exports[0].native_data.empty() && metadata.exports[0].opaque.contains("export"),
              "Metadata-only disables callbacks");
        options = with({decoder});
        options.native_graphs = false;
        options.native_textures = false;
        options.native_meshes = false;
        check(Package::open(file, options).exports[0].native_coverage == "extension",
              "Extensions independent of built-in switches");
        auto texture_fixture = fixture(1012, 1, false, 9);
        const std::string original_class = "DataAsset", texture_class = "Texture2D";
        auto class_position =
            std::search(texture_fixture.header.data.begin(), texture_fixture.header.data.end(),
                        original_class.begin(), original_class.end());
        std::copy(texture_class.begin(), texture_class.end(), class_position);
        texture_fixture.header.patch(texture_fixture.export_offset - 12, 0); // no package-name prefix
        auto texture_file = save(root, "texture-override", texture_fixture);
        auto override_decoder = decoder;
        override_decoder.target = "Texture2D";
        auto overridden = Package::open(texture_file, with({override_decoder}));
        check(overridden.exports[0].native_coverage == "extension" && overridden.textures.empty(),
              "Explicit extension takes precedence over built-in native decoder");
        override_decoder.decode = [](auto &, const auto &) -> Json { throw Error("host-schema", "Stop"); };
        overridden = Package::open(texture_file, with({override_decoder}));
        check(overridden.exports[0].native_coverage == "opaque" && overridden.textures.empty(),
              "Failed explicit extension does not retry the built-in decoder");
        override_decoder.profile.ue5 = {1016, 1018};
        overridden = Package::open(texture_file, with({override_decoder}));
        check(overridden.textures.size() == 1, "Unmatched profile uses the normal native path");
        auto fallback = [&](DecoderCallback callback, const std::string &reason, ReadOptions limits = {}) {
            limits.decoders = with({native(std::move(callback))}).decoders;
            auto result = Package::open(file, limits);
            check(result.exports[0].native_coverage == "opaque" && result.exports[0].native_data.empty() &&
                      result.exports[0].opaque == plain.exports[0].opaque && result.complete_input &&
                      result.exports[0].properties.at("Value").at("value") == 1 &&
                      result.diagnostics.back().find(reason) != std::string::npos,
                  "Failed decoder retained original hash and properties");
        };
        fallback([](auto &, const auto &) { return Json::object(); }, "decoder-consumption");
        fallback(
            [](auto &i, const auto &) {
                i.template get<std::uint64_t>();
                return Json();
            },
            "bounds");
        fallback(
            [](auto &i, const auto &) {
                i.skip(5);
                return Json();
            },
            "bounds");
        fallback(
            [](auto &i, const auto &) {
                i.string();
                return Json();
            },
            "bounds");
        fallback(
            [](auto &i, const auto &) {
                i.count(1);
                return Json();
            },
            "limit");
        fallback(
            [](auto &i, const auto &) {
                i.name();
                return Json();
            },
            "bounds");
        fallback(
            [](auto &i, const auto &) {
                i.reference();
                return Json();
            },
            "reference-index");
        fallback(
            [](auto &i, const auto &) {
                i.guid();
                return Json();
            },
            "bounds");
        fallback(
            [](auto &i, const auto &) {
                i.read(nullptr, 1);
                return Json();
            },
            "decoder-input");
        fallback([](auto &, const auto &) -> Json { throw Error("host-schema", "Unsupported schema"); },
                 "host-schema");
        fallback([](auto &, const auto &) -> Json { throw std::runtime_error("host error"); },
                 "decoder-exception");
        fallback([](auto &, const auto &) -> Json { throw 42; }, "decoder-exception");
        fallback(
            [](auto &i, const auto &) {
                i.skip(4);
                try {
                    i.skip(1);
                } catch (...) {
                }
                return Json();
            },
            "decoder-input");
        fallback(
            [](auto &i, const auto &) {
                i.skip(4);
                return Json(std::numeric_limits<double>::quiet_NaN());
            },
            "decoder-json");
        fallback(
            [](auto &i, const auto &) {
                i.skip(4);
                return Json::binary({1, 2});
            },
            "decoder-json");
        fallback(
            [](auto &i, const auto &) {
                i.skip(4);
                return Json(std::string(1, char(0xff)));
            },
            "decoder-json");
        ReadOptions limits;
        limits.max_property_bytes = 512;
        fallback(
            [](auto &i, const auto &) {
                i.skip(4);
                return Json(std::string(513, 'x'));
            },
            "decoder-limit", limits);
        limits = {};
        limits.max_string_bytes = 32;
        fallback(
            [](auto &i, const auto &) {
                i.skip(4);
                return Json(std::string(33, 'x'));
            },
            "decoder-limit", limits);
        limits = {};
        limits.max_properties = 40;
        fallback(
            [](auto &i, const auto &) {
                i.skip(4);
                return Json(std::vector<int>(40, 0));
            },
            "decoder-limit", limits);
        limits = {};
        limits.max_depth = 8;
        fallback(
            [](auto &i, const auto &) {
                i.skip(4);
                Json value = 0;
                for (int n = 0; n < 10; ++n)
                    value = Json::array({value});
                return value;
            },
            "decoder-limit", limits);
        auto bad_record =
            structure([](auto &i, const auto &) { return Json(i.template get<std::int32_t>()); });
        auto custom_file = save(root, "record-failure", custom_fixture(1012));
        auto failed = Package::open(custom_file, with({bad_record}));
        check(failed.exports[0].properties["Items"]["opaque"]["reason"] == "decoder-consumption" &&
                  failed.exports[0].properties["Items"]["opaque"].contains("sha256") &&
                  failed.exports[0].properties["Value"]["value"] == 99,
              "Struct underconsumption isolated to property");
        bad_record = record;
        bad_record.id = "test.record/2";
        failed = Package::open(custom_file, with({record, bad_record}));
        check(failed.exports[0].properties["Items"]["opaque"]["reason"] == "decoder-ambiguous",
              "Struct ambiguity isolated");
        bad_record = structure([](auto &, const auto &) { return Json::object(); });
        failed =
            Package::open(save(root, "array-no-progress", custom_fixture(1012, true)), with({bad_record}));
        check(failed.exports[0].properties["Items"]["opaque"]["reason"] == "decoder-consumption" &&
                  failed.exports[0].properties["Value"]["value"] == 99,
              "Array callback requires progress and preserves next property");
        auto plain_custom = Package::open(custom_file);
        bad_record = structure([](auto &input, const auto &) -> Json {
            input.skip(input.remaining());
            return input.template get<std::int32_t>();
        });
        failed = Package::open(custom_file, with({bad_record}));
        check(failed.exports[0].properties["Items"]["opaque"]["reason"] == "bounds" &&
                  failed.exports[0].properties["Items"]["opaque"]["sha256"] ==
                      plain_custom.exports[0].properties["Items"]["opaque"]["sha256"] &&
                  failed.exports[0].properties["Value"]["value"] == 99,
              "Struct overread cannot consume the following property");
        auto large = fixture(1012, 1, false, 9);
        large.script.zeros(1024);
        large.header.patch(large.export_offset + 28, static_cast<int>(large.script.data.size()));
        limits = with({decoder});
        limits.max_property_bytes = 512;
        auto large_file = save(root, "large-tail", large);
        failed = Package::open(large_file, limits);
        check(failed.exports[0].native_coverage == "opaque" &&
                  failed.exports[0].opaque["suffix"]["bytes"] == 1028 &&
                  failed.diagnostics.back().find("decoder-limit") != std::string::npos,
              "Native input byte budget applied before callback");
        error([&] { Package::open(save(root, "future", fixture(1019)), with({decoder})); }, "version");
        auto licensee = fixture(1012);
        licensee.header.patch(20, 1);
        error([&] { Package::open(save(root, "licensee", licensee), with({decoder})); }, "licensee-version");
        options = with({bad_record});
        options.fingerprints = false;
        failed = Package::open(custom_file, options);
        check(!failed.exports[0].properties["Items"]["opaque"].contains("sha256"),
              "No fingerprints honored on failure");
        if (argc == 3 && std::string(argv[1]) == "--emit-fixtures") {
            auto destination = fs::u8path(argv[2]);
            fs::create_directories(destination);
            save(destination, "before", custom_fixture(1012));
            save(destination, "after", custom_fixture(1012, false, 44), true);
        }
        fs::remove_all(root);
        std::cout << checks << " decoder checks passed\n";
    } catch (const std::exception &e) {
        fs::remove_all(root);
        std::cerr << "Decoder test failure: " << e.what() << '\n';
        return 1;
    }
}
