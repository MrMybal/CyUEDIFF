#include "schema_hybrid_fixture.hpp"
#include <cyuediff/property_page.hpp>
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
int checks = 0;
constexpr const char *root = "/Script/Test.Collections";
constexpr const char *record = "/Script/Test.Record";
constexpr const char *lookup = "/Script/Test.Collections.Lookup";
constexpr const char *prefix = "/Script/Test.Record.Code";
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
std::shared_ptr<ZenSchemaContext> context(Json layouts = hybrid_layouts(), const char *name = "Tethers",
                                          Bytes mapping = hybrid_mapping()) {
    auto result = std::make_shared<ZenSchemaContext>();
    result->profile = ZenProfile::unreal_5_3;
    result->schemas = std::make_shared<const UnversionedSchemas>(UnversionedSchemas::parse(mapping.data));
    result->bindings["/Script/Test.DemoAsset"] = {root, 0};
    result->binary_layouts = std::make_shared<const BinaryLayouts>(BinaryLayouts::parse(layouts));
    result->struct_tails[record] = name;
    return result;
}
ZenPackage package(const std::shared_ptr<ZenSchemaContext> &c, const Bytes &values = hybrid_values()) {
    ZenReadOptions o;
    o.profile = c->profile;
    o.schema_context = c;
    o.script_objects = std::make_shared<const ScriptObjects>(ScriptObjects::parse(script_fixture().data, o));
    return ZenPackage::parse(collection_zen_fixture(values).data, o);
}
Json changed_layout() {
    auto result = hybrid_layouts();
    result["layouts"]["Tethers"]["fields"][0]["value"]["element"]["element"]["fields"][2]["name"] =
        "restLength";
    return result;
}
bool path(const Json &report, const char *tail) {
    for (const auto &row : report.at("changes")) {
        const auto p = row.at("path").get<std::string>();
        if (p.size() >= std::strlen(tail) &&
            p.compare(p.size() - std::strlen(tail), std::strlen(tail), tail) == 0)
            return true;
    }
    return false;
}
// Original controlled three-schema mapping: reflected key -> described native child.
Bytes key_mapping(bool set) {
    const std::vector<std::string> names = {"Collections", "Lookup", "Tags",   "After",       "Record",
                                            "Code",        "Item",   "Native", "/Script/Test"};
    Bytes payload;
    payload.i(static_cast<int>(names.size()));
    for (const auto &name : names) {
        payload.put<std::uint8_t>(static_cast<std::uint8_t>(name.size()));
        payload.data.insert(payload.data.end(), name.begin(), name.end());
    }
    payload.i(0); // enums
    payload.i(3);
    auto type = [&](int name) {
        auto result = collection_type(9);
        result.i(name);
        return result;
    };
    auto schema = [&](int name, unsigned slots) {
        payload.i(name);
        payload.i(-1);
        payload.put<std::uint16_t>(static_cast<std::uint16_t>(slots));
        payload.put<std::uint16_t>(static_cast<std::uint16_t>(slots));
    };
    auto field = [&](unsigned index, int name, Bytes value) {
        payload.put<std::uint16_t>(static_cast<std::uint16_t>(index));
        payload.put<std::uint8_t>(1);
        payload.i(name);
        payload.append(value);
    };
    schema(0, 3);
    field(0, 1, set ? collection_type(25, type(4)) : map_type(type(4), collection_type(2)));
    field(1, 2, collection_type(25, collection_type(2)));
    field(2, 3, collection_type(2));
    schema(4, 1);
    field(0, 5, type(6));
    schema(6, 1);
    field(0, 7, collection_type(2));
    Bytes paths;
    paths.put<std::uint8_t>(0);
    paths.i(0);
    paths.i(3);
    paths.i(8);
    paths.i(8);
    paths.i(8);
    payload.i(0x54584543);
    payload.put<std::uint8_t>(0);
    payload.i(1);
    for (auto c : std::string("PPTH"))
        payload.put(c);
    payload.i(static_cast<int>(paths.data.size()));
    payload.append(paths);
    Bytes bytes;
    bytes.put<std::uint16_t>(0x30c4);
    bytes.put<std::uint8_t>(0);
    bytes.put<std::uint8_t>(0);
    bytes.i(static_cast<int>(payload.data.size()));
    bytes.i(static_cast<int>(payload.data.size()));
    bytes.append(payload);
    return bytes;
}
Bytes key_values(bool set, int mapped = 41, int key = 7) {
    Bytes result;
    result.put<std::uint16_t>(0x0700);
    auto add_key = [&] {
        result.put<std::uint16_t>(0x0300);
        result.i(key);
    };
    result.i(1);
    add_key(); // removed key
    result.i(1);
    add_key();
    if (!set)
        result.i(mapped);
    result.i(0);
    result.i(0); // empty Tags
    result.i(987);
    return result;
}
std::shared_ptr<ZenSchemaContext> key_context(bool set, bool rename = false, bool changed = false) {
    auto result = context(hybrid_layouts(), "Tethers", key_mapping(set));
    result->struct_tails.clear();
    result->reflected_structs.insert(record);
    Json fields = Json::array({hybrid_field(changed ? "other" : "binaryLayoutsSha256", {{"type", "i32"}})});
    const auto name = rename ? "Renamed" : "Native";
    Json layouts = {{"format", "cyuediff.binary-layouts/1"},
                    {"layouts", {{name, {{"type", "record"}, {"fields", fields}}}}}};
    if (rename)
        layouts["layouts"]["Unrelated"] = {{"type", "u8"}};
    result->binary_layouts = std::make_shared<const BinaryLayouts>(BinaryLayouts::parse(layouts));
    result->struct_layouts["/Script/Test.Item"] = name;
    return result;
}
} // namespace
int main(int argc, char **argv) {
    try {
        const auto base = package(context());
        auto unused = hybrid_layouts();
        unused["layouts"]["Unused"] = {{"type", "u8"}};
        const auto unrelated = compare_zen_packages(base, package(context(unused)));
        check(unrelated["status"] == "identical" && unrelated["contextChanged"] == true &&
                  unrelated["changes"].empty() && unrelated["unavailableScopes"].empty(),
              "Unrelated catalogue entries do not change an asset");
        auto renamed = hybrid_layouts();
        renamed["layouts"]["Renamed"] = renamed["layouts"]["Tethers"];
        renamed["layouts"].erase("Tethers");
        const auto same = compare_zen_packages(base, package(context(renamed, "Renamed")));
        check(same["status"] == "identical" && same["changes"].empty() && same["unavailableScopes"].empty(),
              "Local layout names are interpretation provenance");
        const auto incompatible = compare_zen_packages(base, package(context(changed_layout())));
        check(incompatible["status"] == "incomplete" && incompatible["changes"].empty() &&
                  incompatible["unavailableScopes"].size() == 1 &&
                  incompatible["unavailableScopes"][0]["path"].get<std::string>().find("/value/native") !=
                      std::string::npos,
              "Same bytes with a different native program are not fake member additions/removals");
        auto bytes = hybrid_values();
        bytes.patch(5, 29);
        bytes.patch(bytes.data.size() - 4, 456);
        const auto mixed = compare_zen_packages(base, package(context(changed_layout()), bytes));
        check(mixed["status"] == "incomplete" && path(mixed, "~1Script~1Test.Record.Code/value") &&
                  path(mixed, "~1Script~1Test.Collections.After/value") &&
                  mixed["unavailableScopes"].size() == 1,
              "Known prefix and following property survive an incompatible native tail");
        const auto actual = compare_zen_packages(base, package(context(), hybrid_values(4.5f)));
        check(actual["status"] == "changed" && actual["changes"].size() == 2 &&
                  actual["unavailableScopes"].empty() && path(actual, "/native/batches/0/0/length"),
              "Actual native values and full-body evidence still change");
        auto damaged = hybrid_values();
        damaged.data.resize(20);
        const auto failed = compare_zen_packages(base, package(context(), damaged));
        check(failed["status"] == "incomplete" && failed["unavailableScopes"].size() == 1 &&
                  failed["changes"].size() == 2 && !path(failed, "/properties"),
              "A failed peer does not remove readable properties");
        auto old = base;
        old.exports[0].property_read.erase("interpretation");
        const auto legacy = compare_zen_packages(base, old);
        check(legacy["status"] == "incomplete" && legacy["changes"].empty(),
              "Missing retained interpretation is unverified, including equal decoded JSON");
        auto mapping = hybrid_mapping();
        for (std::size_t i = 0; i + 4 <= mapping.data.size(); ++i)
            if (std::memcmp(mapping.data.data() + i, "Code", 4) == 0)
                std::memcpy(mapping.data.data() + i, "Rate", 4);
        const auto field_rename =
            compare_zen_packages(base, package(context(hybrid_layouts(), "Tethers", mapping), bytes));
        check(field_rename["status"] == "incomplete" && field_rename["unavailableScopes"].size() == 2 &&
                  !path(field_rename, "~1Script~1Test.Record.Code") &&
                  path(field_rename, "~1Script~1Test.Collections.After/value"),
              "Schema field renaming cannot invent stored override changes");
        Bytes array;
        array.i(2);
        array.append(hybrid_record(11));
        array.append(hybrid_record(22));
        auto arrays = collection_single(array);
        const auto array_base = package(context(hybrid_layouts(), "Tethers", hybrid_mapping(true)), arrays);
        const auto array_diff = compare_zen_packages(
            array_base, package(context(changed_layout(), "Tethers", hybrid_mapping(true)), arrays));
        check(array_diff["changes"].empty() && array_diff["unavailableScopes"].size() == 2,
              "Struct programs inside array elements retain scoped correspondence");
        for (const bool set : {false, true}) {
            const auto a = package(key_context(set), key_values(set));
            const auto b = package(key_context(set, true), key_values(set));
            check(a.property_exports_decoded == 2 && b.property_exports_decoded == 2,
                  "Nested native map/set keys decode");
            const auto diff = compare_zen_packages(a, b);
            check(diff["status"] == "identical" && diff["changes"].empty() &&
                      diff["unavailableScopes"].empty(),
                  "Nested key annotations and local catalogue names cannot change membership");
            const auto &value = a.exports[0].property_read.at("properties").at(lookup).at("value");
            const auto id = value.at("entries").begin().key();
            check(id.find("binaryLayout\"") == std::string::npos &&
                      id.find("binaryLayoutsSha256\":7") != std::string::npos,
                  "Only wrapper annotations are excluded; annotation-like native data remains significant");
            const auto bad = compare_zen_packages(a, package(key_context(set, false, true), key_values(set)));
            check(bad["changes"].empty() && bad["unavailableScopes"].size() == 2,
                  "Incompatible key programs suppress both entries and removals");
            const auto changed = compare_zen_packages(a, package(key_context(set), key_values(set, 42, 8)));
            check(changed["status"] == "changed" && changed["unavailableScopes"].empty() &&
                      changed["changes"].size() >= 4,
                  "Changes to real native key data preserve known stored membership changes");
            if (!set) {
                const auto mapped =
                    compare_zen_packages(a, package(key_context(set, true), key_values(set, 42)));
                check(mapped["changes"].size() == 2 && mapped["unavailableScopes"].empty(),
                      "Mapped values remain comparable with equivalent renamed native key programs");
            }
        }
        auto slot_mapping = collection_schema_fixture(collection_type(2));
        auto moved_mapping = slot_mapping;
        for (const auto item : {std::pair<int, int>{0, 1}, {2, 3}}) {
            Bytes find;
            find.put<std::uint16_t>(static_cast<std::uint16_t>(item.first));
            find.put<std::uint8_t>(1);
            find.i(item.second);
            find.put<std::uint8_t>(2);
            unsigned found = 0;
            for (std::size_t i = 0; i + find.data.size() <= moved_mapping.data.size(); ++i)
                if (std::memcmp(moved_mapping.data.data() + i, find.data.data(), find.data.size()) == 0) {
                    moved_mapping.data[i + 3] = static_cast<std::uint8_t>(4 - item.second);
                    ++found;
                }
            check(found == 1, "Original field slot reassignment fixture is unambiguous");
        }
        Bytes slot_values;
        slot_values.put<std::uint16_t>(0x0700);
        slot_values.i(17);
        slot_values.i(0);
        slot_values.i(0);
        slot_values.i(123);
        const auto slot_before = package(context(hybrid_layouts(), "Tethers", slot_mapping), slot_values);
        const auto slot_after = package(context(hybrid_layouts(), "Tethers", moved_mapping), slot_values);
        check(slot_before.property_exports_decoded == 2 && slot_after.property_exports_decoded == 2,
              "Both slot contexts read the exact same controlled bytes successfully");
        const auto slots = compare_zen_packages(slot_before, slot_after);
        check(slots["binaryChanged"] == false && slots["changes"].empty() &&
                  slots["unavailableScopes"].size() == 2 &&
                  slots["unavailableScopes"][0]["reason"] == "cooked-schema-slot-incompatible",
              "Changed schema slot assignments cannot fabricate named value edits");
        const auto peer = package(context(changed_layout()), bytes);
        auto coverage =
            cooked_property_coverage(&base.exports[0].property_read, &peer.exports[0].property_read);
        check(std::find(coverage.raw.begin(), coverage.raw.end(),
                        "/~1Script~1Test.Collections.Lookup/value/native") != coverage.raw.end() &&
                  coverage.comparison_unavailable.size() == 1,
              "Cooked views use raw JSON semantics with scoped correspondence");
        PropertyTableOptions table_options;
        table_options.coverage = coverage;
        auto table = property_table(&base.exports[0].property_read.at("properties"),
                                    &peer.exports[0].property_read.at("properties"), table_options);
        bool known_prefix = false, unknown_native = false, false_native = false;
        for (const auto &row : table.rows) {
            if (row.path.find("/native/") != std::string::npos) {
                unknown_native = unknown_native || row.change == ChangeKind::unknown;
                false_native = false_native || row.change == ChangeKind::added ||
                               row.change == ChangeKind::removed || row.change == ChangeKind::modified;
            }
            if (row.source_path.find("Record.Code") != std::string::npos && row.before == "17" &&
                row.after == "29")
                known_prefix = known_prefix || row.change == ChangeKind::modified;
        }
        check(known_prefix && unknown_native && !false_native,
              "Flat cooked view retains known prefix edits and shows incompatible native rows as Unknown");
        PropertyPageOptions page_options;
        page_options.coverage = coverage;
        const auto native_scope = "/~1Script~1Test.Collections.Lookup/value/native";
        auto page =
            property_page(&base.exports[0].property_read.at("properties"),
                          &peer.exports[0].property_read.at("properties"), native_scope, page_options);
        check(page.available && !page.table.rows.empty() && page.table.rows[0].change == ChangeKind::unknown,
              "Paged cooked views preserve the same unknown scope");
        const auto renamed_peer = package(context(renamed, "Renamed"));
        coverage =
            cooked_property_coverage(&base.exports[0].property_read, &renamed_peer.exports[0].property_read);
        table_options.coverage = coverage;
        table = property_table(&base.exports[0].property_read.at("properties"),
                               &renamed_peer.exports[0].property_read.at("properties"), table_options);
        check(std::none_of(table.rows.begin(), table.rows.end(),
                           [](const auto &r) { return r.change != ChangeKind::unchanged; }),
              "Equivalent native programmes produce no changed or unknown UI rows");
        const auto failed_peer = package(context(), damaged);
        coverage =
            cooked_property_coverage(&base.exports[0].property_read, &failed_peer.exports[0].property_read);
        check(coverage.after_unavailable == std::vector<std::string>{""},
              "An existing unread peer is unavailable to cooked views");
        coverage = cooked_property_coverage(&base.exports[0].property_read, nullptr);
        check(coverage.after_unavailable.empty() && coverage.comparison_unavailable.empty(),
              "An absent object remains a known absence");
        const auto original = BinaryLayouts::parse(hybrid_layouts()).definitions();
        const auto equivalent = BinaryLayouts::parse(renamed).definitions();
        check(original.at("Tethers").sha256 == equivalent.at("Renamed").sha256 &&
                  original.at("Tethers").sha256.size() == 64,
              "Direct program identities are independent of catalogue-local labels");
        check(original.at("Tethers").sha256 !=
                  BinaryLayouts::parse(changed_layout()).definitions().at("Tethers").sha256,
              "Native member names are part of interpretation");
        Json object = {{"format", "cyuediff.binary-layouts/1"},
                       {"layouts",
                        {{"Object", {{"type", "object"}}},
                         {"Ref", {{"type", "ref"}, {"layout", "Object"}}},
                         {"Scalar", {{"type", "i32"}}}}}};
        auto layouts = BinaryLayouts::parse(object);
        BinaryLayoutReadOptions options;
        options.reference = [](std::int32_t) -> Json { return "/Game/Test"; };
        auto definitions = layouts.definitions(options);
        check(definitions.at("Ref").reference_dependent && definitions.at("Ref").sha256.empty() &&
                  !definitions.at("Scalar").sha256.empty(),
              "An unnamed resolver only invalidates reference-dependent programs");
        options.reference_identity = "test-resolver/1";
        const auto one = layouts.definitions(options);
        options.reference_identity = "test-resolver/2";
        const auto two = layouts.definitions(options);
        check(one.at("Ref").sha256 != two.at("Ref").sha256 &&
                  one.at("Scalar").sha256 == two.at("Scalar").sha256,
              "Resolver contracts propagate through layout references without affecting scalars");
        check(one.at("Object").sha256 == one.at("Ref").sha256,
              "An equivalent ref and inline programme have the same identity");
        const auto key_bytes = key_values(false);
        const auto key_schemas = UnversionedSchemas::parse(key_mapping(false).data);
        auto resolved = [&](const std::string &identity) {
            UnversionedReadOptions read;
            read.reflected_structs.insert(record);
            read.struct_layouts["/Script/Test.Item"] = "Object";
            read.binary_layouts = std::make_shared<const BinaryLayouts>(layouts);
            read.reference_identity = identity;
            read.reference = [](std::int32_t i) -> Json {
                return {
                    {"type", "IntProperty"}, {"schemaIndex", 0u}, {"serializedZero", false}, {"value", i}};
            };
            auto decoded =
                decode_unversioned(key_bytes.data, 0, key_bytes.data.size(), key_schemas, root, read);
            auto output = package(key_context(false), key_bytes);
            output.exports[0].property_read["properties"] = std::move(decoded.properties);
            output.exports[0].property_read["interpretation"] = std::move(decoded.interpretation);
            return output;
        };
        const auto unnamed = resolved("");
        const auto unnamed_peer = resolved("");
        const auto reference_unknown = compare_zen_packages(unnamed, unnamed_peer);
        check(reference_unknown["changes"].empty() && reference_unknown["unavailableScopes"].size() == 2,
              "Unnamed custom resolvers cannot certify equal cooked map keys");
        const auto named = resolved("reference/1"), named_peer = resolved("reference/1");
        check(compare_zen_packages(named, named_peer)["status"] == "identical",
              "A declared stable resolver contract permits stored key correspondence");
        const auto changed_reference = compare_zen_packages(named, resolved("reference/2"));
        check(changed_reference["changes"].empty() && changed_reference["unavailableScopes"].size() == 2,
              "Changed resolver contracts do not invent membership changes");
        const auto native_identity = named.exports[0]
                                         .property_read.at("properties")
                                         .at(lookup)
                                         .at("value")
                                         .at("entries")
                                         .begin()
                                         .key();
        check(native_identity.find("schemaIndex") != std::string::npos &&
                  native_identity.find("IntProperty") != std::string::npos,
              "A property-like host reference remains ordinary native key data");
        auto named_coverage =
            cooked_property_coverage(&named.exports[0].property_read, &named_peer.exports[0].property_read);
        check(named_coverage.cooked_properties && named_coverage.ignored.empty(),
              "Cooked view metadata policy does not allocate one ignore path per annotation");
        for (const auto &identity :
             {std::string("bad identity"), std::string(257, 'x'), std::string("bad\nidentity")}) {
            try {
                resolved(identity);
                throw std::runtime_error("Expected resolver identity rejection");
            } catch (const Error &e) {
                check(e.code == "schema-context", "Cooked resolver contracts have a bounded ASCII identity");
            }
        }
        options.control.cancelled = [] { return true; };
        try {
            layouts.definitions(options);
            throw std::runtime_error("Expected cancellation");
        } catch (const Error &e) {
            check(e.code == "cancelled", "Definition hashing observes cancellation");
        }
        if (argc == 2) {
            Json proof = {{"unrelated", unrelated},
                          {"renamed", same},
                          {"changedProgram", incompatible},
                          {"actualValue", actual},
                          {"mixed", mixed},
                          {"failed", failed},
                          {"array", array_diff},
                          {"schemaRename", field_rename},
                          {"slots", slots}};
            std::ofstream(fs::u8path(argv[1]), std::ios::binary) << proof.dump(2) << '\n';
        }
        std::cout << checks << " cooked interpretation checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << " after " << checks << " checks\n";
        return 1;
    }
}
