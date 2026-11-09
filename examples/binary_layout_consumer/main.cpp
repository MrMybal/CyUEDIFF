#include <cyuediff/binary_layout.hpp>
#include <cyuediff/source_limits.hpp>
#include <cyuediff/schema_context.hpp>
#include <cyuediff/string_encoding.hpp>
#include <cstring>
#include <fstream>
#include <iostream>
using namespace cyuediff;
namespace {
std::vector<std::uint8_t> bytes(const fs::path &path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() < 0 || file.tellg() > 16 * 1024 * 1024)
        throw Error("consumer-input", "Expected an input of at most 16 MiB");
    std::vector<std::uint8_t> value(static_cast<std::size_t>(file.tellg()));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char *>(value.data()), static_cast<std::streamsize>(value.size())) ||
        file.peek() != EOF)
        throw Error("consumer-input", "Cannot read stable input");
    return value;
}
struct Input final : DecoderInput {
    std::vector<std::uint8_t> data;
    std::size_t at = 0;
    explicit Input(std::vector<std::uint8_t> value) : data(std::move(value)) {}
    std::uint64_t position() const override {
        return at;
    }
    std::uint64_t remaining() const override {
        return data.size() - at;
    }
    void read(void *out, std::size_t count) override {
        if (count > remaining())
            throw Error("bounds", "Truncated consumer bytes");
        if (count)
            std::memcpy(out, data.data() + at, count);
        at += count;
    }
    void skip(std::uint64_t count) override {
        if (count > remaining())
            throw Error("bounds", "Truncated consumer skip");
        at += static_cast<std::size_t>(count);
    }
    std::uint32_t count(std::uint32_t maximum) override {
        const auto value = get<std::uint32_t>();
        if (maximum && value > maximum)
            throw Error("consumer-input", "Excessive count");
        return value;
    }
    std::string string() override {
        throw Error("consumer-input", "Use bounded layout FString");
    }
    std::string name() override {
        throw Error("consumer-input", "Package name table required");
    }
    std::string reference() override {
        throw Error("consumer-input", "Package reference table required");
    }
    std::string guid() override {
        throw Error("consumer-input", "GUID resolver not configured");
    }
};
Json self_test() {
    auto document = Json{
        {"format", "cyuediff.binary-layouts/1"},
        {"layouts",
         {{"Example",
           {{"type", "record"},
            {"fields",
             Json::array(
                 {{{"name", "strength"}, {"value", {{"type", "i32"}}}},
                  {{"name", "samples"},
                   {"value", {{"type", "array"}, {"count", "i32"}, {"element", {{"type", "u8"}}}}}}})}}}}}};
    const auto layouts = BinaryLayouts::parse(document);
    Input input({42, 0, 0, 0, 3, 0, 0, 0, 1, 2, 3});
    const auto value = layouts.decode(input, "Example");
    const auto encoded = decode_narrow_string("caf\xe9 \x80", NarrowStringEncoding::windows_1252, 32);
    const bool checked = input.remaining() == 0 && value["strength"] == 42 &&
                         value["samples"] == Json::array({1, 2, 3}) && encoded == "caf\xc3\xa9 \xe2\x82\xac";
    if (!checked)
        throw Error("consumer-check", "Installed layout consumer verification failed");
    return {{"verified", true}, {"catalogSha256", layouts.sha256()}, {"value", value}, {"encoded", encoded}};
}
Json run(const std::vector<std::string> &args) {
    if (args.empty())
        return self_test();
    if (args.size() == 3 && args[0] == "encoding") {
        const auto raw = bytes(fs::u8path(args[2]));
        return {{"value", decode_narrow_string(std::string(raw.begin(), raw.end()),
                                               parse_narrow_string_encoding(args[1]), 1024 * 1024)}};
    }
    if (args.size() == 3 && args[0] == "raw") {
        const auto catalog = BinaryLayouts::read(fs::u8path(args[1]));
        Input input(bytes(fs::u8path(args[2])));
        const auto value = catalog.decode(input, "Record");
        return {{"value", value}, {"consumedBytes", input.position()}, {"remainingBytes", input.remaining()}};
    }
    if (args.size() >= 3 && args[0] == "package") {
        std::vector<std::string> files;
        std::map<std::string, std::string> flags;
        for (std::size_t i = 1; i < args.size(); ++i) {
            const auto &key = args[i];
            if (key.rfind("--", 0) != 0) {
                files.push_back(key);
                continue;
            }
            if (key != "--max-properties" && key != "--max-property-bytes" &&
                key != "--after-max-properties" && key != "--after-max-property-bytes")
                throw Error("consumer-arguments", "Unknown source read policy option");
            if (i + 1 == args.size() || !flags.emplace(key, args[i + 1]).second)
                throw Error("consumer-arguments", "Missing or duplicate source limit option");
            ++i;
        }
        if (files.size() != 2 && files.size() != 4)
            throw Error("consumer-arguments", "Select a catalogue/source pair or two pairs");
        auto number = [&](const std::string &key, std::uint64_t fallback, std::uint64_t maximum) {
            const auto it = flags.find(key);
            if (it == flags.end())
                return fallback;
            std::uint64_t value = 0;
            if (it->second.empty())
                throw Error("consumer-arguments", "Empty source limit");
            for (char c : it->second) {
                if (c < '0' || c > '9' || value > (maximum - std::uint64_t(c - '0')) / 10)
                    throw Error("consumer-arguments", "Invalid source limit");
                value = value * 10 + std::uint64_t(c - '0');
            }
            if (!value || value > maximum)
                throw Error("consumer-arguments", "Source limit exceeds its finite range");
            return value;
        };
        SourceReadLimits before_limits;
        before_limits.max_properties = static_cast<std::uint32_t>(
            number("--max-properties", before_limits.max_properties, SourceReadLimits::maximum_work));
        before_limits.max_property_bytes =
            number("--max-property-bytes", before_limits.max_property_bytes, SourceReadLimits::maximum_bytes);
        auto after_limits = before_limits;
        after_limits.max_properties = static_cast<std::uint32_t>(
            number("--after-max-properties", after_limits.max_properties, SourceReadLimits::maximum_work));
        after_limits.max_property_bytes = number(
            "--after-max-property-bytes", after_limits.max_property_bytes, SourceReadLimits::maximum_bytes);
        if (files.size() == 2 &&
            (flags.count("--after-max-properties") || flags.count("--after-max-property-bytes")))
            throw Error("consumer-arguments", "After limits require an after source");
        auto open = [&](const std::string &catalog_path, const std::string &asset,
                        const SourceReadLimits &limits) {
            const auto catalog = BinaryLayouts::read(fs::u8path(catalog_path));
            ReadOptions options;
            limits.apply(options);
            options.decoders = catalog.decoder_registry();
            options.narrow_string_encoding = catalog.narrow_string_encoding();
            return Package::open(fs::u8path(asset), options);
        };
        const auto before = open(files[0], files[1], before_limits);
        Json result = {{"before", before.to_json()}};
        if (files.size() == 4) {
            const auto after = open(files[2], files[3], after_limits);
            result["after"] = after.to_json();
            result["comparison"] = compare(before, after).to_json();
        }
        return result;
    }
    if (args.size() == 5 && args[0] == "cooked") {
        const auto manifest = ZenSchemaManifest::read(fs::u8path(args[1]));
        const auto context = manifest.load();
        ZenReadOptions options;
        options.profile = context->profile;
        options.schema_context = context;
        const auto global = Container::open(fs::u8path(args[4]));
        bool found = false;
        for (std::size_t i = 0; i < global.entries().size(); ++i)
            if (global.entries()[i].chunk_type == 5 && !global.entries()[i].deleted) {
                if (found)
                    throw Error("consumer-input", "Ambiguous script objects");
                found = true;
                options.script_objects =
                    std::make_shared<ScriptObjects>(ScriptObjects::parse(global.read(i), options));
            }
        if (!found)
            throw Error("consumer-input", "Missing script objects");
        const auto container = Container::open(fs::u8path(args[2]));
        for (std::size_t i = 0; i < container.entries().size(); ++i)
            if (container.entries()[i].id == args[3])
                return read_zen_package(container, i, options).to_json();
        throw Error("consumer-input", "Missing cooked entry");
    }
    throw Error("consumer-arguments",
                "Use no arguments, encoding NAME RAW, raw LAYOUTS RAW, package LAYOUTS ASSET [AFTER_LAYOUTS "
                "AFTER_ASSET], or cooked CONTEXT TOC ENTRY GLOBAL_TOC");
}
int checked(const std::vector<std::string> &args) {
    try {
        std::cout << run(args).dump(2) << '\n';
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
        args.push_back(fs::path(argv[i]).u8string());
    return checked(args);
}
#else
int main(int argc, char **argv) {
    return checked({argv + 1, argv + argc});
}
#endif
