#include <cyuediff/property_page.hpp>
#include <charconv>
#include <fstream>
#include <iostream>
#include <optional>
namespace {
std::optional<cyuediff::Json> read(const std::string &name) {
    if (name == "-")
        return std::nullopt;
    const auto path = cyuediff::fs::u8path(name);
    if (cyuediff::fs::file_size(path) > 64 * 1024 * 1024)
        throw cyuediff::Error("input-limit", "Example JSON input exceeds 64 MiB");
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw cyuediff::Error("input", "Cannot open JSON input");
    return cyuediff::Json::parse(input, [](int depth, cyuediff::Json::parse_event_t, cyuediff::Json &) {
        if (depth > 128)
            throw cyuediff::Error("input-limit", "Example JSON depth exceeds 128");
        return true;
    });
}
int run(const std::vector<std::string> &args) {
    using namespace cyuediff;
    if (args.size() < 4 || args.size() > 5) {
        std::cerr << "Usage: property_page_consumer BEFORE.json|- AFTER.json|- JSON_POINTER FIRST "
                     "[COVERAGE.json]\n";
        return 2;
    }
    try {
        const auto a = read(args[0]), b = read(args[1]);
        PropertyPageOptions options;
        if (args.size() == 5) {
            auto masks = read(args[4]);
            if (!masks || !masks->is_object())
                throw Error("input", "Expected an object of JSON pointer masks");
            auto load = [&](const char *key, auto &out) {
                auto i = masks->find(key);
                if (i != masks->end())
                    out = i->get<std::vector<std::string>>();
            };
            load("beforeUnavailable", options.coverage.before_unavailable);
            load("afterUnavailable", options.coverage.after_unavailable);
            load("comparisonUnavailable", options.coverage.comparison_unavailable);
            load("ignored", options.coverage.ignored);
            load("raw", options.coverage.raw);
            options.serialization_fields = masks->value("serializationFields", false);
        }
        const auto &text = args[3];
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), options.first);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
            throw Error("arguments", "FIRST must be an unsigned decimal integer");
        const auto page = property_page(a ? &*a : nullptr, b ? &*b : nullptr, args[2], options);
        Json rows = Json::array();
        for (std::size_t i = 0; i < page.table.rows.size(); ++i) {
            const auto &row = page.table.rows[i];
            rows.push_back({{"path", row.path},
                            {"label", row.label},
                            {"type", row.type},
                            {"before", row.before},
                            {"after", row.after},
                            {"change", change_name(row.change)},
                            {"beforePresent", row.before_present},
                            {"afterPresent", row.after_present},
                            {"beforeAvailable", row.before_available},
                            {"afterAvailable", row.after_available},
                            {"contextOnly", row.context_only},
                            {"sourcePath", row.source_path},
                            {"expandable", page.expandable[i]}});
        }
        std::cout
            << Json{{"scope", page.scope}, {"available", page.available},          {"array", page.array},
                    {"first", page.first}, {"totalChildren", page.total_children}, {"rows", rows}}
                   .dump(2)
            << '\n';
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
