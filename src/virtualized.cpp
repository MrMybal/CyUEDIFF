#include <cyuediff/virtualized.hpp>
#include <algorithm>
namespace cyuediff {
fs::path virtualized_payload_relative_path(const std::string &id) {
    if (id.size() != 40 ||
        !std::all_of(id.begin(), id.end(),
                     [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) ||
        id.find_first_not_of('0') == std::string::npos)
        throw Error("virtualized-id", "Virtualized payload requires a nonzero lowercase 40-digit FIoHash");
    return fs::path(id.substr(0, 2)) / id.substr(2, 2) / id.substr(4, 2) / (id.substr(6) + ".upayload");
}
fs::path virtualized_payload_file(const fs::path &root, const std::string &id,
                                  const OperationControl &control) {
    control.checkpoint("Resolving virtualized payload");
    const auto relative = virtualized_payload_relative_path(id);
    if (root.empty())
        throw Error("virtualized-root", "Select a local virtualized payload store explicitly");
    std::error_code ec;
    const auto base = fs::canonical(root, ec);
    if (ec || !fs::is_directory(base, ec) || ec)
        throw Error("virtualized-root", "Virtualized payload root is not a readable directory");
    const auto target = fs::canonical(base / relative, ec);
    if (ec)
        throw Error("virtualized-missing", "Virtualized payload is absent from the selected store: " + id);
    const auto within = target.lexically_relative(base);
    if (within.empty() || within.is_absolute() ||
        std::any_of(within.begin(), within.end(), [](const fs::path &part) { return part == ".."; }))
        throw Error("virtualized-path", "Virtualized payload resolves outside the selected store");
    if (!fs::is_regular_file(target, ec) || ec)
        throw Error("virtualized-file", "Virtualized payload is not a readable regular file");
    control.check();
    return target;
}
} // namespace cyuediff
