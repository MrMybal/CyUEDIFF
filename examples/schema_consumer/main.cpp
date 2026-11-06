#include <cyuediff/schema.hpp>
#include <cyuediff/schema_context.hpp>
#include <fstream>
#include <iostream>
namespace {
int run(const std::vector<std::string> &args) {
    if (args.size() != 2 && args.size() != 3 && args.size() != 4) {
        std::cerr << "Usage: schema_consumer MAPPINGS.usmap VALUES.bin [CONTEXT.json [NAMES.json]]\n";
        return 2;
    }
    try {
        auto schemas = cyuediff::UnversionedSchemas::read(cyuediff::fs::u8path(args[0]));
        cyuediff::UnversionedReadOptions options;
        if (args.size() >= 3) {
            const auto manifest = cyuediff::ZenSchemaManifest::read(cyuediff::fs::u8path(args[2]));
            const auto context = manifest.load();
            if (context->schemas->sha256() != schemas.sha256() ||
                context->bindings.at("/Script/Consumer.Consumer").schema != "Consumer")
                throw std::runtime_error("Unexpected schema context binding");
            schemas = *context->schemas;
            options.reflected_structs = context->reflected_structs;
            options.native_structs = context->native_structs;
            options.binary_layouts = context->binary_layouts;
            options.struct_layouts = context->struct_layouts;
            options.narrow_string_encoding = context->narrow_string_encoding;
            options.text_layout = cyuediff::TextLayout::ue5_history;
            options.text_argument_data_layout = cyuediff::TextArgumentDataLayout::variant_int64;
            options.script_reference_layout = cyuediff::ScriptReferenceLayout::package_indices;
            options.field_path_layout = cyuediff::FieldPathLayout::owner_and_names;
            if (context->profile == cyuediff::ZenProfile::unreal_5_5) {
                options.optional_layout = cyuediff::OptionalLayout::bool32_value;
                options.byte_string_layout = cyuediff::ByteStringLayout::counted_utf8_ascii;
            }
        }
        std::vector<std::string> names;
        if (args.size() == 4) {
            std::ifstream input(cyuediff::fs::u8path(args[3]), std::ios::binary | std::ios::ate);
            if (!input || input.tellg() < 0 || input.tellg() > 1024 * 1024)
                throw std::runtime_error("Invalid package names input");
            std::string bytes(static_cast<std::size_t>(input.tellg()), '\0');
            input.seekg(0);
            if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
                throw std::runtime_error("Cannot read package names");
            auto values = cyuediff::Json::parse(bytes);
            if (!values.is_array() || values.size() > 100000)
                throw std::runtime_error("Invalid package name table");
            names = values.get<std::vector<std::string>>();
            options.names = &names;
        }
        std::ifstream file(cyuediff::fs::u8path(args[1]), std::ios::binary | std::ios::ate);
        if (!file || file.tellg() < 0 || file.tellg() > 1024 * 1024)
            throw std::runtime_error("Invalid property input");
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file.tellg()));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            throw std::runtime_error("Cannot read properties");
        auto decoded = cyuediff::decode_unversioned(bytes, 0, bytes.size(), schemas, "Consumer", options);
        std::cout << decoded.properties.dump(2) << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 2;
    }
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
        args.push_back(cyuediff::fs::path(argv[i]).u8string());
    return run(args);
}
#else
int main(int argc, char **argv) {
    return run({argv + 1, argv + argc});
}
#endif
