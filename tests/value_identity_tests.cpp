#include <cyuediff/schema.hpp>
#include <cyuediff/cache.hpp>
#include <cyuediff/zen.hpp>
#include "zen_fixture.hpp"
#include "package_fixture.hpp"
#include "tagged_hybrid_fixture.hpp"
#include "../src/value_identity.hpp"
#include "../src/key_identity.hpp"
#include <iostream>
#include <chrono>
using namespace cyuediff;
using namespace cyuediff::test;
namespace {
int checks = 0;
void check(bool good, const char *message) {
    ++checks;
    if (!good)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f, const char *code) {
    try {
        f();
    } catch (const Error &e) {
        check(e.code == code, e.what());
        return;
    }
    throw std::runtime_error("Expected identity operation failure");
}
UnversionedSchemas schemas() {
    return UnversionedSchemas::from_runtime_capture(
        {{"format", "cyuediff.engine-default-capture/1"},
         {"completeCapture", true},
         {"filterEditorOnly", false},
         {"enumerations", Json::object()},
         {"declarations",
          {{"/Script/Test.Key",
            {{"path", "/Script/Test.Key"},
             {"super", ""},
             {"kind", "struct"},
             {"slots", 1},
             {"properties", Json::array({{{"name", "N/~"},
                                          {"index", 0},
                                          {"arrayDim", 1},
                                          {"type", {{"name", "NameProperty"}}}}})}}}}}});
}
void numbered(Bytes &b, const NameWriter &name, const std::string &base, int number) {
    name(b, base);
    b.patch(b.data.size() - 4, number);
}
Fixture named_source(bool duplicate = false, bool reorder = false) {
    return fixture(1012, 1, reorder, 0,
                   {"NameProperty", "MapProperty", "Literal", "Numbered", "Names", "NameMap", "Broken",
                    "Item", "Item_1", "Alpha/~", "Beta", "NameSet", "SetProperty"},
                   [duplicate](Bytes &script, const NameWriter &name) {
                       auto type = [&](Bytes &out, const char *kind) {
                           name(out, kind);
                           out.i(0);
                       };
                       auto tag = [&](const char *field, const char *kind, const Bytes &payload,
                                      bool array = false, bool map = false) {
                           name(script, field);
                           name(script, kind);
                           script.i(array ? 1 : map ? 2 : 0);
                           if (array || map)
                               type(script, "NameProperty");
                           if (map)
                               type(script, "NameProperty");
                           script.i(static_cast<int>(payload.data.size()));
                           script.put<std::uint8_t>(0);
                           script.append(payload);
                       };
                       Bytes literal;
                       numbered(literal, name, "Item_1", 0);
                       tag("Literal", "NameProperty", literal);
                       Bytes index;
                       numbered(index, name, "Item", 2);
                       tag("Numbered", "NameProperty", index);
                       Bytes list;
                       list.i(2);
                       list.append(literal);
                       list.append(index);
                       tag("Names", "ArrayProperty", list, true);
                       Bytes map;
                       map.i(1);
                       name(map, "Beta");
                       map.i(3);
                       name(map, "Alpha/~");
                       map.append(index);
                       map.append(literal);
                       map.append(literal);
                       map.append(duplicate ? literal : index);
                       map.append(index);
                       tag("NameMap", "MapProperty", map, false, true);
                       Bytes set;
                       set.i(0);
                       set.i(2);
                       set.append(literal);
                       set.append(index);
                       tag("NameSet", "SetProperty", set, true);
                       Bytes bad;
                       bad.append(index);
                       bad.i(123);
                       tag("Broken", "NameProperty", bad);
                   });
}
void verify_paths(const Json &value, const Json &context) {
    for (auto it = context.at("names").begin(); it != context.at("names").end(); ++it) {
        const auto &n = it.value();
        const auto number = n.at("number").get<std::uint32_t>();
        const auto display =
            n.at("base").get<std::string>() + (number ? "_" + std::to_string(number - 1) : "");
        check(value.at(Json::json_pointer(it.key())) == display,
              "An identity path dereferences its actual decoded value");
    }
}
} // namespace
int main() {
    const auto root =
        fs::current_path() / (".test-tmp-value-identities-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        // Journal contracts: exact escaped paths, nested discard, moves and unused large paths.
        ValueIdentityOptions caps;
        caps.names = true;
        caps.max_text_bytes = 7;
        detail::ValueIdentityJournal exact_journal(caps, {});
        {
            detail::ValueIdentityJournal::Scope path(exact_journal, "a/~");
            exact_journal.name("x", 0);
        }
        auto exact_paths = exact_journal.finish();
        check(exact_paths.at("status") == "complete" && exact_paths.at("textBytes") == 7 &&
                  exact_paths.at("names").contains("/a~1~0"),
              "Exact escaped pointer budget is usable");
        caps.max_text_bytes = 6;
        detail::ValueIdentityJournal short_journal(caps, {});
        {
            detail::ValueIdentityJournal::Scope path(short_journal, "a/~");
            short_journal.name("x", 0);
        }
        check(short_journal.finish().at("status") == "incomplete",
              "One-byte-short observation budget is explicit");
        caps.max_text_bytes = 1;
        detail::ValueIdentityJournal unused_journal(caps, {});
        { detail::ValueIdentityJournal::Scope unused(unused_journal, std::string(1024, 'q')); }
        {
            detail::ValueIdentityJournal::Buffer unused(unused_journal);
            unused.commit_at({std::string(1024, 'q')});
        }
        unused_journal.name("x", 0);
        check(unused_journal.finish().at("status") == "complete",
              "Large paths without names do not consume the identity budget");
        caps.max_text_bytes = 1024;
        detail::ValueIdentityJournal discarded(caps, {});
        {
            detail::ValueIdentityJournal::Buffer pending(discarded);
            discarded.name("lost", 1);
        }
        discarded.name("kept", 2);
        auto discarded_context = discarded.finish();
        check(discarded_context.at("namesRead") == 2 && discarded_context.at("names").size() == 1 &&
                  discarded_context.at("names").at("").at("base") == "kept",
              "Discarded transactions keep work but not false observations");
        detail::ValueIdentityJournal first(caps, {});
        first.name("root", 0);
        detail::ValueIdentityJournal moved(std::move(first));
        {
            detail::ValueIdentityJournal::Scope path(moved, "child");
            moved.name("other", 2);
        }
        check(moved.finish().at("names").size() == 2, "Moved journal publishes into its own storage");
        detail::ValueIdentityJournal conflicting(caps, {});
        conflicting.name("first", 0);
        conflicting.name("second", 0);
        auto conflict = conflicting.finish();
        check(conflict.at("status") == "incomplete" && conflict.at("names").empty(),
              "Conflicting locations cannot publish an arbitrary identity");
        const auto mapping = schemas();
        const std::vector<std::string> names = {"Item", "Item_1", "Alpha/~", "Beta", "ClÃ©/Î»~"};
        UnversionedReadOptions options;
        options.names = &names;
        options.value_identities.names = true;
        options.reflected_structs.insert("/Script/Test.Key");
        auto read = [&](const Bytes &bytes, const SchemaType &type, const UnversionedReadOptions &o) {
            return decode_unversioned_value(bytes.data, 0, bytes.data.size(), mapping, type, o);
        };
        Bytes wide;
        wide.i(4);
        wide.i(0);
        const auto wide_value = read(wide, {5, {}, {}}, options);
        check(wide_value.value == "ClÃ©/Î»~" &&
                  wide_value.value_identities.at("names").at("").at("base") == "ClÃ©/Î»~",
              "UTF8 identity bytes are preserved without case conversion");
        Bytes array;
        array.i(2);
        array.i(1);
        array.i(0);
        array.i(0);
        array.i(2);
        auto pair = read(array, {8, {}, {{5, {}, {}}}}, options);
        check(pair.value == Json::array({"Item_1", "Item_1"}), "Legacy name display is preserved");
        check(pair.value_identities.at("names").at("/0").at("base") == "Item_1" &&
                  pair.value_identities.at("names").at("/1").at("base") == "Item" &&
                  pair.value_identities.at("names").at("/1").at("number") == 2,
              "Identical displays retain different base/number identities");
        verify_paths(pair.value, pair.value_identities);
        auto disabled = options;
        disabled.value_identities.names = false;
        auto old = read(array, {8, {}, {{5, {}, {}}}}, disabled);
        check(old.value == pair.value && old.interpretation == pair.interpretation &&
                  old.value_identities.is_null(),
              "Opt-in observations do not alter values or interpretation");
        auto limited = options;
        limited.value_identities.max_names = 1;
        auto partial = read(array, {8, {}, {{5, {}, {}}}}, limited);
        check(partial.value == pair.value && partial.value_identities.at("status") == "incomplete" &&
                  partial.value_identities.at("names").size() == 1,
              "Identity record limits retain decoded values and known observations");
        limited = options;
        limited.value_identities.max_text_bytes = 1;
        auto bounded = read(array, {8, {}, {{5, {}, {}}}}, limited);
        check(bounded.value == pair.value && bounded.value_identities.at("status") == "incomplete" &&
                  bounded.value_identities.at("names").empty(),
              "Identity text limit never makes a stored value opaque");
        Bytes scalar;
        scalar.i(0);
        scalar.i(2);
        limited = options;
        limited.value_identities.max_text_bytes = 4;
        auto exact = read(scalar, {5, {}, {}}, limited);
        check(exact.value_identities.at("status") == "complete" &&
                  exact.value_identities.at("names").contains(""),
              "Root scalar identity accepts the exact base-byte limit");
        Bytes reflected;
        reflected.put<std::uint16_t>(0x0300);
        reflected.i(0);
        reflected.i(2);
        auto record = read(reflected, {9, "/Script/Test.Key", {}}, options);
        verify_paths(record.value, record.value_identities);
        check(record.value_identities.at("names").contains("/~1Script~1Test.Key.N~1~0/value"),
              "Qualified reflected fields escape pointer tokens");
        Bytes zero;
        zero.put<std::uint16_t>(0x0380);
        zero.put<std::uint8_t>(1);
        auto masked = read(zero, {9, "/Script/Test.Key", {}}, options);
        check(masked.value_identities.at("names").begin().value().at("source") == "zero-mask",
              "Serialized zero names have a separate observation origin");
        // Reflected map keys and name arrays exercise delayed key locations and nested transactions.
        Bytes map;
        map.i(1);
        map.append(reflected);
        map.i(1);
        map.append(reflected);
        map.append(array);
        const SchemaType names_type{8, {}, {{5, {}, {}}}};
        const SchemaType map_type{24, {}, {{9, "/Script/Test.Key", {}}, names_type}};
        auto mapped = read(map, map_type, options);
        verify_paths(mapped.value, mapped.value_identities);
        check(mapped.value_identities.at("names").size() == 4,
              "Map removals, reflected keys and mapped name arrays retain distinct locations");
        // Functional key capture must not inherit optional metadata selection or limits.
        const SchemaType name_key{5, {}, {}};
        const SchemaType keyed_type{24, {}, {name_key, {2, {}, {}}}};
        auto name_key_id = [](const std::string &base, unsigned number) {
            const auto display = base + (number ? "_" + std::to_string(number - 1) : "");
            return "ue-name/1:[" + Json(display).dump() + "," +
                   Json{{"", {{"base", base}, {"number", number}}}}.dump() + "]";
        };
        const auto literal_id = name_key_id("Item_1", 0), numbered_id = name_key_id("Item", 2);
        Bytes ambiguous;
        ambiguous.i(0);
        ambiguous.i(2);
        ambiguous.i(1);
        ambiguous.i(0);
        ambiguous.i(11);
        ambiguous.i(0);
        ambiguous.i(2);
        ambiguous.i(22);
        auto keys = read(ambiguous, keyed_type, options);
        check(keys.value.at("entries").size() == 2 &&
                  keys.value.at("entries").at(literal_id).at("value") == 11 &&
                  keys.value.at("entries").at(numbered_id).at("value") == 22,
              "Real typed name components distinguish equal-display map keys");
        verify_paths(keys.value, keys.value_identities);
        Bytes name_set;
        name_set.i(0);
        name_set.i(2);
        name_set.i(1);
        name_set.i(0);
        name_set.i(0);
        name_set.i(2);
        const auto decoded_set = read(name_set, {25, {}, {name_key}}, options);
        check(decoded_set.value.at("entries").size() == 2 &&
                  decoded_set.value.at("entries").at(literal_id) == "Item_1" &&
                  decoded_set.value.at("entries").at(numbered_id) == "Item_1",
              "Unversioned sets retain distinct equal-display typed name keys");
        verify_paths(decoded_set.value, decoded_set.value_identities);
        const auto plain_keys = read(ambiguous, keyed_type, disabled);
        check(plain_keys.value == keys.value && plain_keys.value_identities.is_null(),
              "Functional name keys do not require opt-in metadata");
        limited = options;
        limited.value_identities.max_names = 0;
        limited.value_identities.max_text_bytes = 0;
        const auto limited_keys = read(ambiguous, keyed_type, limited);
        check(limited_keys.value == keys.value && limited_keys.value_identities.at("status") == "incomplete",
              "Exhausted optional metadata never falls back to ambiguous legacy IDs");
        auto reordered_names = names;
        std::swap(reordered_names[0], reordered_names[1]);
        auto reordered_options = disabled;
        reordered_options.names = &reordered_names;
        auto reordered_bytes = ambiguous;
        reordered_bytes.patch(8, 0);
        reordered_bytes.patch(20, 1);
        check(read(reordered_bytes, keyed_type, reordered_options).value == keys.value,
              "Name table indices do not participate in functional key identity");
        auto duplicate_keys = ambiguous;
        duplicate_keys.patch(20, 1);
        duplicate_keys.patch(24, 0);
        rejects([&] { read(duplicate_keys, keyed_type, options); }, "schema-identity");
        auto budget = options;
        budget.max_text_bytes = 4;
        rejects([&] { read(ambiguous, keyed_type, budget); }, "schema-limit");
        budget = options;
        budget.max_values = 5;
        rejects([&] { read(ambiguous, keyed_type, budget); }, "schema-limit");
        // Reflected and nested name-containing keys use the same domain separation.
        Bytes reflected_pair;
        reflected_pair.i(0);
        reflected_pair.i(2);
        reflected_pair.append(reflected);
        reflected_pair.i(11);
        Bytes literal_record;
        literal_record.put<std::uint16_t>(0x0300);
        literal_record.i(1);
        literal_record.i(0);
        reflected_pair.append(literal_record);
        reflected_pair.i(22);
        const auto reflected_keys =
            read(reflected_pair, {24, {}, {{9, "/Script/Test.Key", {}}, {2, {}, {}}}}, options);
        check(reflected_keys.value.at("entries").size() == 2,
              "Reflected name-containing keys retain both equal-display records");
        verify_paths(reflected_keys.value, reflected_keys.value_identities);
        Bytes nested_keys;
        nested_keys.i(0);
        nested_keys.i(1);
        nested_keys.append(ambiguous);
        nested_keys.i(33);
        const auto nested = read(nested_keys, {24, {}, {keyed_type, {2, {}, {}}}}, options);
        check(nested.value.at("entries").begin().key().find("ue-name/1:") == 0 &&
                  nested.value_identities.at("names").size() == 2,
              "Nested functional frames restore parent captures and delayed key paths");
        verify_paths(nested.value, nested.value_identities);
        // Zero-mask and wire origin are observations, not name identity components.
        Bytes zero_key;
        zero_key.i(0);
        zero_key.i(1);
        zero_key.put<std::uint16_t>(0x0380);
        zero_key.put<std::uint8_t>(1);
        zero_key.i(7);
        auto none_names = names;
        none_names.push_back("None");
        auto none_options = options;
        none_options.names = &none_names;
        Bytes wire_none;
        wire_none.i(0);
        wire_none.i(1);
        wire_none.put<std::uint16_t>(0x0300);
        wire_none.i(5);
        wire_none.i(0);
        wire_none.i(7);
        const SchemaType reflected_type{24, {}, {{9, "/Script/Test.Key", {}}, {2, {}, {}}}};
        const auto zero_keys = read(zero_key, reflected_type, none_options);
        const auto wire_keys = read(wire_none, reflected_type, none_options);
        check(zero_keys.value.at("entries").begin().key() == wire_keys.value.at("entries").begin().key(),
              "A zero-mask None key and its wire equivalent have identical IDs");
        detail::ValueIdentityJournal restored(caps, {});
        {
            detail::ValueIdentityJournal::Scope outer(restored, "outer");
            {
                detail::ValueIdentityJournal::KeyProbe probe(restored, caps);
                restored.name("discarded", 0);
                // An exception or failed key simply unwinds without publishing.
            }
            restored.name("retained", 0);
        }
        const auto restored_result = restored.finish();
        check(restored_result.at("names").size() == 1 &&
                  restored_result.at("names").at("/outer").at("base") == "retained",
              "Discarding a functional frame restores the exact parent path and target");
        auto raw_key = Json{{"", {{"base", "Item_1"}, {"number", 0}}}};
        const auto raw_id = detail::named_key_identity(
            raw_key, Json::object(), [](const Json &v, bool) { return v.dump(); }, [](auto) {});
        check(raw_id == raw_key.dump() && raw_id != literal_id,
              "Raw native JSON resembling an identity is not promoted into the typed domain");
        unsigned work = 0;
        std::uint64_t text = 0;
        options.value_identities.charge = [&](auto n, auto b) {
            work += n;
            text += b;
        };
        auto charged = read(map, map_type, options);
        check(charged.value_identities.at("namesRead") == work &&
                  charged.value_identities.at("textBytes") == text,
              "Identity charges include key relocation preparation");
        bool cancel = false;
        options.control.cancelled = [&] { return cancel; };
        options.value_identities.charge = [&](auto, auto) { cancel = true; };
        rejects([&] { read(array, {8, {}, {{5, {}, {}}}}, options); }, "cancelled");
        options.control = {};
        options.value_identities.charge = {};
        auto file = save(root, "Names", named_source());
        auto before = Package::open(file);
        ReadOptions source;
        source.value_identities.names = true;
        unsigned observations = 0;
        source.value_identities.charge = [&](auto n, auto) { observations += n; };
        auto after = Package::open(file, source);
        check(before.semantic_snapshot() == after.semantic_snapshot(),
              "Source identity collection preserves the full stored snapshot");
        const auto &ids = after.value_identities;
        const auto path = std::string("/objects/TestAsset/properties/");
        check(ids.at("names").at(path + "Literal/value").at("base") == "Item_1" &&
                  ids.at("names").at(path + "Numbered/value").at("number") == 2,
              "Source scalar names preserve actual wire components");
        check(!ids.at("names").contains(path + "Broken/value") && observations > ids.at("names").size(),
              "A failed source property discards observations without refunding charges");
        verify_paths(after.semantic_snapshot(), ids);
        const auto &source_map = after.exports[0].properties.at("NameMap").at("value").at("entries");
        check(source_map.size() == 3 && source_map.at(literal_id).at("key") == "Item_1" &&
                  source_map.at(numbered_id).at("key") == "Item_1",
              "Tagged source maps retain valid equal-display native key identities");
        auto literal_package = after, numbered_package = after;
        literal_package.exports[0].properties["NameMap"]["value"]["entries"] = {
            {literal_id, source_map.at(literal_id)}};
        numbered_package.exports[0].properties["NameMap"]["value"]["entries"] = {
            {numbered_id, source_map.at(numbered_id)}};
        const auto key_change = compare(literal_package, numbered_package);
        check(key_change.changes.size() == 2 && key_change.unavailable_scopes.empty(),
              "A true stored name-key replacement is visible despite equal displayed keys");
        check(after.exports[0].properties.at("NameSet").at("value").at("entries").size() == 2,
              "Tagged source sets share the typed key codec");
        auto reordered_source = Package::open(save(root, "ReorderedNames", named_source(false, true)));
        check(reordered_source.exports[0].properties.at("NameMap").at("value") ==
                  after.exports[0].properties.at("NameMap").at("value"),
              "Reordered source name tables preserve map correspondence");
        auto duplicate_source = Package::open(save(root, "DuplicateNames", named_source(true)));
        check(duplicate_source.exports[0].properties.at("NameMap").contains("opaque"),
              "Actual duplicate typed keys remain an explicit unread property");
        check(compare(before, after).changes.empty(),
              "Observation-only differences are not stored source changes");
        source.value_identities.charge = {};
        source.value_identities.max_names = 1;
        auto reduced = Package::open(file, source);
        check(reduced.semantic_snapshot() == before.semantic_snapshot() &&
                  reduced.value_identities.at("status") == "incomplete",
              "Source identity limit does not damage independent stored reads");
        save_tagged_hybrid(root, "HybridNames",
                           tagged_hybrid_fixture(1013, false, SIZE_MAX, false, false, true));
        ReadOptions hybrid_options;
        hybrid_options.decoders = BinaryLayouts::parse(hybrid_catalogue()).decoder_registry();
        hybrid_options.value_identities.names = true;
        const auto hybrid_names = Package::open(root / "HybridNames.uasset", hybrid_options);
        check(!hybrid_names.value_identities.at("names").empty(),
              "Hybrid prefixes publish actual typed names while raw native names remain excluded");
        verify_paths(hybrid_names.semantic_snapshot(), hybrid_names.value_identities);
        PackageCache cache;
        source.value_identities.max_names = 100000;
        auto cached_plain = cache.open(file);
        auto cached_names = cache.open(file, source);
        check(cached_plain != cached_names && cache.open(file, source) == cached_names,
              "Identity selection participates in cache profiles");
        source.value_identities.max_names = 1;
        check(cache.open(file, source) != cached_names, "Identity limits participate in cache profiles");
        source.value_identities.max_names = 100000;
        source.value_identities.charge = [&](auto n, auto) { observations += n; };
        auto old_count = observations;
        auto one = cache.open(file, source);
        auto two = cache.open(file, source);
        check(one != two && observations > old_count,
              "Charged observations bypass retention and charge every operation");
        // Two real decoder export windows share an enclosing identity budget.
        auto cooked_bytes = zen_fixture();
        std::uint32_t header = 0, export_table = 0;
        std::memcpy(&header, cooked_bytes.data.data() + 4, 4);
        std::memcpy(&export_table, cooked_bytes.data.data() + 32, 4);
        cooked_bytes.data.resize(header);
        Bytes cooked_value;
        cooked_value.put<std::uint16_t>(0x0300);
        cooked_value.i(2);
        cooked_value.i(3);
        for (unsigned i = 0; i < 2; ++i) {
            const std::uint64_t offset = i * cooked_value.data.size(), length = cooked_value.data.size();
            std::memcpy(cooked_bytes.data.data() + export_table + i * 72, &offset, 8);
            std::memcpy(cooked_bytes.data.data() + export_table + i * 72 + 8, &length, 8);
            cooked_bytes.append(cooked_value);
        }
        auto context = std::make_shared<ZenSchemaContext>();
        context->profile = ZenProfile::unreal_5_3;
        context->schemas = std::make_shared<UnversionedSchemas>(mapping);
        context->bindings["/Script/Test.DemoAsset"] = {"/Script/Test.Key", 0};
        ZenReadOptions cooked_options;
        cooked_options.profile = context->profile;
        cooked_options.schema_context = context;
        cooked_options.script_objects =
            std::make_shared<ScriptObjects>(ScriptObjects::parse(script_fixture().data));
        auto plain_cooked = ZenPackage::parse(cooked_bytes.data, cooked_options);
        cooked_options.value_identities.names = true;
        auto observed_cooked = ZenPackage::parse(cooked_bytes.data, cooked_options);
        check(observed_cooked.property_exports_decoded == 2 && observed_cooked.property_exports_failed == 0,
              "Cooked exports forward explicit name observations");
        for (const auto &e : observed_cooked.exports)
            verify_paths(e.property_read.at("properties"), e.property_read.at("valueIdentities"));
        check(compare_zen_packages(plain_cooked, observed_cooked).at("changes").empty(),
              "Cooked observation-only metadata does not fabricate stored changes");
        cooked_options.value_identities.max_names = 1;
        unsigned cooked_names = 0;
        cooked_options.value_identities.charge = [&](auto n, auto) { cooked_names += n; };
        auto limited_cooked = ZenPackage::parse(cooked_bytes.data, cooked_options);
        check(cooked_names == 1 && limited_cooked.property_exports_decoded == 2 &&
                  limited_cooked.exports[1].property_read.at("valueIdentities").at("status") == "incomplete",
              "Cooked identity budget is aggregate across exports without discarding stored values");
        std::cout << checks << " value identity checks passed\n";
        fs::remove_all(root);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "After check " << checks << ": " << e.what() << '\n';
        fs::remove_all(root);
        return 1;
    }
}
