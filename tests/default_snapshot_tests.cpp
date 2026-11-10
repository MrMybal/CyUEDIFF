#include <cyuediff/defaults.hpp>
#include "default_snapshot_fixture.hpp"
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test::default_snapshot;
namespace {
int checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f, const char *code = nullptr) {
    try {
        f();
    } catch (const Error &e) {
        check(!code || e.code == code, e.what());
        return;
    }
    throw std::runtime_error("Expected rejected capture after check " + std::to_string(checks));
}
} // namespace
int main() {
    try {
        auto json = capture();
        auto opts = options();
        auto snapshot = parse(json, opts);
        check(snapshot.fields().at("/Script/Test.Child.Count").at("value") == 41,
              "Captured serialized field is decoded even if reflection display is unavailable");
        check(snapshot.fields().at("/Script/Test.Child.Large").at("value").get<std::int64_t>() ==
                  9007199254740993ll,
              "Captured int64 is exact beyond JavaScript number precision");
        check(snapshot.fields().at("/Script/Test.Base.Inherited").at("value") == 27,
              "Child CDO inherited field value is not replaced with its base CDO");
        check(snapshot.fields()
                          .at("/Script/Test.Child.Record")
                          .at("value")
                          .at("/Script/Test.Record.Count")
                          .at("value") == 19 &&
                  snapshot.structures()
                          .at("/Script/Test.Record")
                          .at("fields")
                          .at("/Script/Test.Record.Count")
                          .at("value") == 7,
              "Member defaults and initialized type defaults remain distinct");
        check(snapshot.fields().at("/Script/Test.Child.Point").at("available") == false,
              "Native struct wire is unavailable until explicitly bound");
        check(snapshot.provenance().at("binding") == "unbound" &&
                  snapshot.provenance().at("defaultsApplied") == false,
              "Capture reading does not certify/apply a default context to any asset");
        opts.values.native_structs["/Script/Test.NativePoint"] = NativeStructLayout::int_point;
        auto complete = parse(json, opts);
        check(complete.fields().at("/Script/Test.Child.Point").at("value") == Json::array({9, -4}),
              "An exact qualified native binding decodes the complete wire");
        opts.expected_sha256 = complete.sha256();
        check(parse(json, opts).sha256() == complete.sha256(), "Optional exact capture fingerprint");
        opts.expected_sha256[0] = opts.expected_sha256[0] == 'a' ? 'b' : 'a';
        rejects([&] { parse(json, opts); }, "default-capture");
        opts = options();
        auto mutate = [&](auto f, const char *code = "default-capture") {
            auto j = json;
            f(j);
            rejects([&] { parse(j, opts); }, code);
        };
        rejects([&] { parse(json, {}); }, "default-capture");
        auto wrong = opts;
        wrong.expected_engine_version = "5.5-other";
        rejects([&] { parse(json, wrong); }, "default-capture");
        wrong = opts;
        wrong.expected_ue5++;
        rejects([&] { parse(json, wrong); }, "default-capture");
        wrong = opts;
        wrong.filter_editor_only = true;
        rejects([&] { parse(json, wrong); }, "default-capture");
        mutate([](Json &j) { j["classDefault"] = false; });
        mutate([](Json &j) { j["object"]["flags"] = "0000000000000001"; });
        mutate([](Json &j) { j["completeCapture"] = false; });
        mutate([](Json &j) { j["fields"].erase("/Script/Test.Child.Count"); });
        mutate([](Json &j) { j["fields"]["Foreign"] = j["fields"]["/Script/Test.Child.Count"]; });
        mutate([](Json &j) { j["fields"]["/Script/Test.Child.Count"]["owner"] = "/Script/Test.Base"; });
        mutate([](Json &j) { j["fields"]["/Script/Test.Child.Count"]["type"]["name"] = "FloatProperty"; });
        mutate([](Json &j) { j["fields"]["/Script/Test.Child.Count"]["arrayIndex"] = 1; });
        mutate([](Json &j) { j["fields"]["/Script/Test.Child.Count"]["flags"] = "0000000000000002"; });
        mutate(
            [](Json &j) { j["declarations"]["/Script/Test.Child"]["properties"][0]["serialize"] = false; });
        mutate([](Json &j) { j["structures"]["/Script/Test.Record"]["structGuid"] = std::string(32, 'a'); });
        mutate([](Json &j) { j["fields"]["/Script/Test.Child.Count"]["wire"]["ue5"] = 1012; });
        mutate([](Json &j) { j["fields"]["/Script/Test.Child.Count"]["wire"]["isCooking"] = true; });
        mutate(
            [](Json &j) { j["fields"]["/Script/Test.Child.Count"]["wire"]["comparisonDefaults"] = "class"; });
        mutate([](Json &j) {
            j["fields"]["/Script/Test.Child.Count"]["wire"]["customVersions"][0]["version"] = 2;
        });
        mutate([](Json &j) { j["fields"]["/Script/Test.Child.Count"]["wire"]["hex"] = "29zz0000"; });
        mutate([](Json &j) { j["fields"]["/Script/Test.Child.Count"]["wire"]["hex"] = "2"; });
        mutate([](Json &j) { j["fields"]["/Script/Test.Child.Count"]["wire"]["hex"] = "2900000000"; });
        mutate([](Json &j) { j["fields"]["/Script/Test.Child.Count"]["wire"]["hex"] = "29"; }, "bounds");
        mutate([](Json &j) {
            auto &type = j["declarations"]["/Script/Test.Child"]["properties"][2]["type"];
            type["structGuid"] = std::string(32, 'a');
            j["fields"]["/Script/Test.Child.Record"]["type"] = type;
        });
        mutate([](Json &j) {
            auto &type = j["declarations"]["/Script/Test.Child"]["properties"][2]["type"];
            type["nativeSerialization"] = true;
            j["fields"]["/Script/Test.Child.Record"]["type"] = type;
        });
        mutate([](Json &j) {
            j["declarations"]["/Script/Test.NativePoint"]["binarySerialization"] = false;
            j["declarations"]["/Script/Test.NativePoint"]["nativeSerialization"] = false;
            j["declarations"]["/Script/Test.Child"]["properties"][3]["type"]["nativeSerialization"] = false;
            j["fields"]["/Script/Test.Child.Point"]["type"]["nativeSerialization"] = false;
        });
        auto unsupported = json;
        unsupported["fields"]["/Script/Test.Child.Count"]["wire"] = {{"available", false},
                                                                     {"reason", "capture failed"}};
        auto partial = parse(unsupported, opts);
        check(!partial.fields().at("/Script/Test.Child.Count").at("available").get<bool>(),
              "Failed field capture stays unavailable");
        auto missing_initializer = json;
        auto &missing = missing_initializer["structures"]["/Script/Test.Record"];
        missing["available"] = false;
        missing["reason"] = "initializer unavailable";
        missing.erase("fields");
        auto missing_snapshot = parse(missing_initializer, opts);
        check(missing_snapshot.structures().at("/Script/Test.Record").at("available") == false &&
                  missing_snapshot.fields().at("/Script/Test.Child.Record").at("available") == true,
              "Unavailable initialized type defaults do not erase an independent captured member wire");
        wrong = opts;
        wrong.values.reflected_structs.insert("/Script/Test.NativePoint");
        rejects([&] { parse(json, wrong); }, "default-capture");
        wrong = opts;
        wrong.values.native_structs["NativePoint"] = NativeStructLayout::int_point;
        rejects([&] { parse(json, wrong); }, "default-capture");
        wrong = opts;
        wrong.capture.max_bytes = 2;
        rejects([&] { parse(json, wrong); }, "default-limit");
        wrong = opts;
        wrong.capture.max_entries = 2;
        rejects([&] { parse(json, wrong); }, "default-limit");
        wrong = opts;
        wrong.capture.max_depth = 2;
        rejects([&] { parse(json, wrong); }, "default-limit");
        wrong = opts;
        wrong.capture.max_text_bytes = 2;
        rejects([&] { parse(json, wrong); }, "default-limit");
        wrong = opts;
        wrong.max_wire_bytes = 4;
        rejects([&] { parse(json, wrong); }, "default-limit");
        unsigned work = 0;
        std::uint64_t text = 0;
        opts.values.charge = [&](std::uint32_t n, std::uint64_t b) {
            work += n;
            text += b;
        };
        auto charged = parse(json, opts);
        check(charged.provenance().at("valuesRead") == work &&
                  charged.provenance().at("valueTextBytes") == text,
              "Aggregate charges include successful reads and unsupported native attempts");
        opts.values.charge = {};
        wrong = opts;
        wrong.values.max_values = work - 1;
        rejects([&] { parse(json, wrong); }, "schema-limit");
        wrong = opts;
        wrong.values.max_text_bytes = text - 1;
        rejects([&] { parse(json, wrong); }, "schema-limit");
        wrong = options();
        wrong.capture.control.cancelled = [] { return true; };
        rejects([&] { parse(json, wrong); }, "cancelled");
        wrong = options();
        unsigned calls = 0;
        wrong.values.control.cancelled = [&] { return ++calls > 2; };
        rejects([&] { parse(json, wrong); }, "cancelled");
        auto duplicate = json.dump();
        duplicate.insert(1, "\"classDefault\":true,");
        rejects([&] { DefaultSnapshot::parse({duplicate.begin(), duplicate.end()}, options()); },
                "default-capture");
        auto invalid = json.dump();
        invalid.insert(1, std::string("\"invalid\":\"") + char(0xff) + "\",");
        rejects([&] { DefaultSnapshot::parse({invalid.begin(), invalid.end()}, options()); },
                "default-capture");
        auto named_capture = json;
        auto named_property = property("StoredName", 4, {{"name", "NameProperty"}});
        named_capture["declarations"]["/Script/Test.Child"]["properties"].push_back(named_property);
        named_capture["declarations"]["/Script/Test.Child"]["slots"] = 5;
        auto named_field = node("/Script/Test.Child", named_property, "0000000002000000");
        named_field["wire"]["names"] = Json::array({"Item"});
        named_capture["fields"]["/Script/Test.Child.StoredName"] = named_field;
        auto name_options = options();
        name_options.values.value_identities.names = true;
        auto named_snapshot = parse(named_capture, name_options);
        const auto &stored_name = named_snapshot.fields().at("/Script/Test.Child.StoredName");
        check(stored_name.at("value") == "Item_1" &&
                  stored_name.at("valueIdentities").at("names").at("").at("base") == "Item" &&
                  named_snapshot.provenance().at("identityNamesRead") == 1,
              "Default capture preserves actual field name identity alongside the display value");
        name_options.values.value_identities.max_names = 0;
        auto unnamed_snapshot = parse(named_capture, name_options);
        check(unnamed_snapshot.fields().at("/Script/Test.Child.StoredName").at("available") == true &&
                  unnamed_snapshot.fields()
                          .at("/Script/Test.Child.StoredName")
                          .at("valueIdentities")
                          .at("status") == "incomplete",
              "Default snapshot identity limits do not discard a decoded field");
        std::cout << checks << " default snapshot checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
