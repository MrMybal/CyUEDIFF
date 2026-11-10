#include <cyuediff/schema.hpp>
#include "schema_hybrid_fixture.hpp"
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::test;
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
    throw std::runtime_error("Expected failure");
}
Json declaration(const char *path, unsigned slots, Json fields, const char *kind = "class",
                 const char *super = "") {
    return {{"path", path},
            {"slots", slots},
            {"properties", std::move(fields)},
            {"kind", kind},
            {"super", super}};
}
Json field(const char *name, unsigned index, unsigned dim, Json type) {
    return {{"name", name}, {"index", index}, {"arrayDim", dim}, {"type", std::move(type)}};
}
Json capture() {
    return {{"format", "cyuediff.engine-default-capture/1"},
            {"completeCapture", true},
            {"filterEditorOnly", false},
            {"declarations",
             {{"/Script/Test.Base",
               declaration("/Script/Test.Base", 1,
                           Json::array({field("Inherited", 0, 1, {{"name", "IntProperty"}})}))},
              {"/Script/Test.Child",
               declaration(
                   "/Script/Test.Child", 2,
                   Json::array({field("Records", 0, 2,
                                      {{"name", "ArrayProperty"},
                                       {"arguments", Json::array({{{"name", "StructProperty"},
                                                                   {"schema", "/Script/Test.Élément"}}})}})}),
                   "class", "/Script/Test.Base")},
              {"/Script/Test.Élément",
               declaration("/Script/Test.Élément", 1,
                           Json::array({field("Valeur", 0, 1, {{"name", "IntProperty"}})}), "struct")}}},
            {"enumerations", Json::object()}};
}
} // namespace
int main() {
    try {
        auto json = capture();
        const auto schemas = UnversionedSchemas::from_runtime_capture(json);
        check(schemas.schema("Child").super == "/Script/Test.Base" &&
                  schemas.schema("Child").properties.size() == 2,
              "Qualified runtime inheritance/static arrays are retained");
        check(schemas.schema("Élément").properties.at(0).name == "Valeur" &&
                  schemas.to_json()["schema"] == "cyuediff.runtime-schemas/1",
              "Runtime Unicode identities do not pretend to be an ASCII USMAP file");
        auto other = json;
        other["fields"] = {{"ChangedDefault", 123}};
        other["object"] = {{"path", "elsewhere"}};
        check(schemas.sha256() == UnversionedSchemas::from_runtime_capture(other).sha256(),
              "Default instance values do not change declaration identity");
        auto read = [&](const Bytes &bytes, const SchemaType &type,
                        const UnversionedReadOptions &options = UnversionedReadOptions{}) {
            return decode_unversioned_value(bytes.data, 0, bytes.data.size(), schemas, type, options);
        };
        Bytes scalar;
        scalar.i(41);
        scalar.i(987);
        auto value = read(scalar, {2, {}, {}});
        check(value.value == 41 && value.consumed_bytes == 4 && value.interpretation["type"]["kind"] == 2,
              "Standalone typed value honors its actual byte consumption");
        Bytes record;
        record.put<std::uint16_t>(0x0300);
        record.i(17);
        UnversionedReadOptions options;
        options.reflected_structs.insert("/Script/Test.Élément");
        auto decoded = read(record, {9, "/Script/Test.Élément", {}}, options);
        check(decoded.value["/Script/Test.Élément.Valeur"]["value"] == 17 &&
                  decoded.consumed_bytes == record.data.size(),
              "An explicitly reflected standalone value retains qualified typed fields");
        Bytes array;
        array.i(2);
        array.append(record);
        array.append(record);
        auto list = read(array, {8, {}, {{9, "/Script/Test.Élément", {}}}}, options);
        check(list.value.size() == 2 && list.value[1]["/Script/Test.Élément.Valeur"]["value"] == 17,
              "Typed standalone containers use the same bounded reader");
        for (std::size_t end = 0; end < array.data.size(); ++end)
            rejects([&] {
                decode_unversioned_value(array.data, 0, end, schemas,
                                         {8, {}, {{9, "/Script/Test.Élément", {}}}}, options);
            });
        for (const auto &bad : std::vector<SchemaType>{{2, {}, {{2, {}, {}}}},
                                                       {9, {}, {}},
                                                       {8, {}, {}},
                                                       {24, {}, {{2, {}, {}}}},
                                                       {2, "foreign", {}},
                                                       {31, {}, {}}})
            rejects([&] { read(scalar, bad, options); }, "schema-type");
        auto limited = options;
        limited.max_values = 1;
        rejects([&] { read(record, {9, "/Script/Test.Élément", {}}, limited); }, "schema-limit");
        limited = options;
        limited.control.cancelled = [] { return true; };
        rejects([&] { read(record, {9, "/Script/Test.Élément", {}}, limited); }, "cancelled");
        unsigned work = 0;
        std::uint64_t text = 0;
        options.charge = [&](std::uint32_t n, std::uint64_t b) {
            work += n;
            text += b;
        };
        auto charged = read(record, {9, "/Script/Test.Élément", {}}, options);
        check(charged.values_read == work && charged.text_bytes == text,
              "Standalone context and value preparation share exact host charges");
        auto mutate = [&](auto change, const char *code = "schema-runtime") {
            auto bad = json;
            change(bad);
            rejects([&] { UnversionedSchemas::from_runtime_capture(bad); }, code);
        };
        mutate([](Json &j) { j["completeCapture"] = false; });
        mutate([](Json &j) { j["declarations"]["/Script/Test.Child"]["super"] = "/Script/Test.Child"; });
        mutate([](Json &j) { j["declarations"]["/Script/Test.Child"]["super"] = "/Script/Other.Missing"; });
        mutate([](Json &j) { j["declarations"]["/Script/Test.Child"]["properties"][0]["arrayDim"] = 3; });
        mutate([](Json &j) { j["declarations"]["/Script/Test.Child"]["properties"][0]["index"] = -1; });
        mutate([](Json &j) {
            j["declarations"]["/Script/Test.Child"]["properties"][0]["type"]["arguments"][0]["schema"] =
                "/Script/Test.Base";
        });
        mutate([](Json &j) {
            j["declarations"]["/Script/Test.Child"]["properties"][0]["type"]["arguments"] = Json::array();
        });
        mutate([](Json &j) {
            j["declarations"]["/Script/Test.Base"]["properties"].push_back(
                j["declarations"]["/Script/Test.Base"]["properties"][0]);
        });
        mutate([](Json &j) { j["filterEditorOnly"] = 1; });
        mutate([](Json &j) { j["declarations"]["/Script/Test.Base"]["path"] = "/Script/Other.Base"; });
        SchemaReadOptions budget;
        budget.max_bytes = 100;
        rejects([&] { UnversionedSchemas::from_runtime_capture(json, budget); }, "schema-limit");
        budget = {};
        budget.max_entries = 4;
        rejects([&] { UnversionedSchemas::from_runtime_capture(json, budget); }, "schema-limit");
        budget = {};
        budget.max_text_bytes = 20;
        rejects([&] { UnversionedSchemas::from_runtime_capture(json, budget); }, "schema-limit");
        budget = {};
        budget.control.cancelled = [] { return true; };
        rejects([&] { UnversionedSchemas::from_runtime_capture(json, budget); }, "cancelled");
        auto native = json;
        native["declarations"]["/Script/Test.Élément"]["kind"] = "native-struct";
        const auto native_schemas = UnversionedSchemas::from_runtime_capture(native);
        rejects(
            [&] {
                decode_unversioned_value(record.data, 0, record.data.size(), native_schemas,
                                         {9, "/Script/Test.Élément", {}}, options);
            },
            "schema-type");
        auto unknown = json;
        unknown["declarations"]["/Script/Test.Base"]["properties"][0]["type"]["name"] = "UnknownProperty";
        check(UnversionedSchemas::from_runtime_capture(unknown).schema("Base").properties.at(0).type.kind ==
                  255,
              "Unavailable runtime types preserve their slots");
        auto collision = json;
        collision["declarations"]["/Script/Other.Base"] = declaration("/Script/Other.Base", 0, Json::array());
        auto ambiguous = UnversionedSchemas::from_runtime_capture(collision);
        rejects([&] { ambiguous.schema("Base"); }, "schema-ambiguous");
        std::cout << checks << " runtime schema/standalone value checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << " after " << checks << " checks\n";
        return 1;
    }
}
