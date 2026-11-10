#include "effective_commands.hpp"
#include <cyuediff/defaults.hpp>
#include "../../src/atomic_output.hpp"
#include "../../src/json_output.hpp"
#include <iostream>
#include <map>
#include <set>
using namespace cyuediff;
namespace {
struct Side {
    DefaultContext context;
    Package package;
    EffectiveDefaults effective;
};
Side read_side(const fs::path &asset, DefaultContext context, std::string object) {
    ReadOptions read;
    read.value_identities.names = true;
    read.value_identities.max_names = read.max_properties;
    read.value_identities.max_text_bytes = read.max_property_bytes;
    auto package = Package::open(asset, read);
    if (object.empty()) {
        const auto roots = default_root_exports(package, context);
        if (roots.size() > 1)
            throw Error("arguments", "Choose a root export explicitly");
        if (!roots.empty())
            object = roots.front();
    }
    if (object.empty())
        throw Error("default-context", "No root export matches the prepared class");
    auto effective = resolve_source_defaults(package, object, context);
    return {std::move(context), std::move(package), std::move(effective)};
}
} // namespace
int run_effective_command(const std::vector<std::string> &args) {
    const bool diff = args.at(0) == "effective-diff";
    std::vector<std::string> positional;
    std::map<std::string, std::string> flags;
    const std::set<std::string> inspect_flags = {"--out", "--object", "--expect-context"};
    const std::set<std::string> diff_flags = {
        "--out",          "--before-context",        "--after-context",       "--before-object",
        "--after-object", "--expect-before-context", "--expect-after-context"};
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i].compare(0, 2, "--") != 0) {
            positional.push_back(args[i]);
            continue;
        }
        const auto flag = args[i];
        if (!(diff ? diff_flags : inspect_flags).count(flag) || flags.count(flag) || ++i >= args.size())
            throw Error("arguments", "Invalid effective command option");
        flags[flag] = args[i];
    }
    if (positional.size() != 2 ||
        (diff && (!flags.count("--before-context") || !flags.count("--after-context"))))
        throw Error("arguments",
                    diff ? "effective-diff requires BEFORE AFTER --before-context JSON --after-context JSON"
                         : "effective-inspect requires ASSET CONTEXT.json");
    const auto output = fs::u8path(flags["--out"]);
    if (!output.empty() && fs::exists(output))
        throw Error("output-path", "Effective commands refuse existing output files");
    auto load_context = [&](const std::string &name, const std::string &expected) {
        DefaultContextReadOptions o;
        o.expected_sha256 = expected;
        return DefaultContext::read(fs::u8path(name), o);
    };
    detail::JsonObject report;
    int exit = 0;
    if (!diff) {
        auto side = read_side(fs::u8path(positional[0]),
                              load_context(positional[1], flags["--expect-context"]), flags["--object"]);
        report.own("schema", "cyuediff.effective-inspection/1");
        report.produce("stored", [&] { return side.package.semantic_snapshot(); });
        report.own("effective", side.effective.to_json());
        report.borrow("context", side.context.provenance());
        if (output.empty())
            report.write(std::cout, 2);
        else
            detail::atomic_output(output, [&](std::ostream &stream) { report.write(stream, 2); });
        return side.effective.complete ? 0 : 2;
    }
    auto before_context = load_context(flags["--before-context"], flags["--expect-before-context"]);
    const auto same_path = fs::absolute(fs::u8path(flags["--before-context"])).lexically_normal() ==
                           fs::absolute(fs::u8path(flags["--after-context"])).lexically_normal();
    auto after_context =
        same_path ? before_context : load_context(flags["--after-context"], flags["--expect-after-context"]);
    if (same_path && !flags["--expect-after-context"].empty() &&
        flags["--expect-after-context"] != after_context.sha256())
        throw Error("default-context", "Expected after context fingerprint differs");
    auto before = read_side(fs::u8path(positional[0]), std::move(before_context), flags["--before-object"]);
    auto after = read_side(fs::u8path(positional[1]), std::move(after_context), flags["--after-object"]);
    auto comparison = compare_effective_defaults(before.effective, after.effective);
    report.own("schema", "cyuediff.effective-comparison/1");
    report.own("comparison", comparison.to_json());
    report.borrow("beforeContext", before.context.provenance());
    report.borrow("afterContext", after.context.provenance());
    report.borrow("beforeSourceFingerprints", before.package.fingerprints);
    report.borrow("afterSourceFingerprints", after.package.fingerprints);
    report.borrow("beforeObject", before.effective.provenance);
    report.borrow("afterObject", after.effective.provenance);
    if (output.empty())
        report.write(std::cout, 2);
    else
        detail::atomic_output(output, [&](std::ostream &stream) { report.write(stream, 2); });
    exit = comparison.status() == "identical" ? 0 : comparison.status() == "changed" ? 1 : 2;
    return exit;
}
