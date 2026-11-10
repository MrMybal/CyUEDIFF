#include <cyuediff/decoders.hpp>
#include <iostream>
using namespace cyuediff;
int main(int argc, char **argv) {
    if (argc == 3 && std::string(argv[1]) == "--inspect-empty-registry") {
        try {
            ReadOptions options;
            options.decoders = std::make_shared<const DecoderRegistry>(std::vector<AssetDecoder>{});
            std::cout << Package::open(fs::u8path(argv[2]), options).to_json().dump() << '\n';
            return 0;
        } catch (const std::exception &error) {
            std::cerr << error.what() << '\n';
            return 2;
        }
    }
    if (argc != 3) {
        std::cerr << "Usage: custom_diff BEFORE.uasset AFTER.uasset\n"
                     "Example schema for the generated decoder test fixtures only.\n";
        return 2;
    }
    try {
        // These are host-declared schema contracts, not hashes of callback machine code.
        // Update the contract/ID whenever implementation or captured-state interpretation changes.
        const auto definition = [](const std::string &contract) {
            return sha256_bytes(std::vector<std::uint8_t>(contract.begin(), contract.end()));
        };
        AssetDecoder record;
        record.id = "example.record/1";
        record.target = "ExampleStruct";
        record.profile.ue4 = {522, 522};
        record.profile.ue5 = {1012, 1012};
        record.definition_sha256 =
            definition("example.record/1;ue4=522;ue5=1012;ExampleStruct;little-endian;"
                       "i32:number;fname:name;fstring:text;package-string:reference;guid:guid");
        record.decode = [](DecoderInput &input, const DecoderContext &) -> Json {
            auto value = input.get<std::int32_t>();
            auto name = input.name(), text = input.string(), reference = input.reference(),
                 guid = input.guid();
            return {
                {"number", value}, {"name", name}, {"text", text}, {"reference", reference}, {"guid", guid}};
        };
        AssetDecoder tail;
        tail.id = "example.tail/1";
        tail.kind = DecoderKind::native_export;
        // The fixture uses DataAsset for its controlled native tail; this is not a DataAsset schema.
        tail.target = "/Script/CoreUObject.DataAsset";
        tail.profile = record.profile;
        tail.definition_sha256 = definition(
            "example.tail/1;ue4=522;ue5=1012;DataAsset;TestAsset;/Game/TestAsset;little-endian;i32:counter");
        tail.decode = [](DecoderInput &input, const DecoderContext &context) -> Json {
            if (context.object.name != "TestAsset" || context.summary.package_name != "/Game/TestAsset")
                throw Error("example-schema", "Expected the controlled test fixture");
            return {{"counter", input.get<std::int32_t>()}};
        };
        ReadOptions options;
        options.decoders = std::make_shared<const DecoderRegistry>(std::vector<AssetDecoder>{record, tail});
        auto before = Package::open(fs::u8path(argv[1]), options);
        auto after = Package::open(fs::u8path(argv[2]), options);
        auto result = compare(before, after);
        result.write_json(std::cout);
        std::cout << '\n';
        return result.status() == "identical" ? 0 : (result.status() == "changed" ? 1 : 2);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
