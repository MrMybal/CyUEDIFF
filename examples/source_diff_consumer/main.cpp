#include <cyuediff/core.hpp>
#include <cyuediff/binary_layout.hpp>
#include <cyuediff/source_limits.hpp>
#include <charconv>
#include <iostream>
namespace {
int run(const std::vector<std::string> &args) {
    using namespace cyuediff;
    try {
        if (args.size() != 4 && args.size() != 5)
            throw Error("arguments", "Usage: source_diff_consumer BEFORE_LAYOUTS BEFORE.uasset AFTER_LAYOUTS "
                                     "AFTER.uasset [MAX_WORK]");
        SourceReadLimits limits;
        if (args.size() == 5) {
            const auto &text = args[4];
            auto value = std::from_chars(text.data(), text.data() + text.size(), limits.max_properties);
            if (value.ec != std::errc{} || value.ptr != text.data() + text.size())
                throw Error("arguments", "MAX_WORK must be an unsigned integer");
        }
        limits.validate();
        auto open = [&](const std::string &catalogue, const std::string &asset) {
            const auto layouts = BinaryLayouts::read(fs::u8path(catalogue));
            ReadOptions options;
            limits.apply(options);
            options.decoders = layouts.decoder_registry();
            options.narrow_string_encoding = layouts.narrow_string_encoding();
            return Package::open(fs::u8path(asset), options);
        };
        auto before = open(args[0], args[1]), after = open(args[2], args[3]);
        const auto diff = compare(before, after);
        diff.write_json(std::cout);
        std::cout.put('\n');
        if (!std::cout)
            throw Error("io", "Cannot write source comparison");
        const auto status = diff.status();
        return status == "identical" ? 0 : status == "incomplete" || status == "unverified" ? 2 : 1;
    } catch (const Error &e) {
        std::cerr << Json{{"error", e.code}, {"message", e.what()}}.dump() << '\n';
        return 2;
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
int main(int argc, char **argv) { return run({argv + 1, argv + argc}); }
#endif
