#include <cyuediff/git_binary_delta.hpp>
#include <iostream>
using namespace cyuediff;
int run(const std::vector<std::string> &args) {
    try {
        if (args.size() == 1 && args[0] == "--help") {
            std::cout << "REPO BEFORE AFTER PATH [TEMPORARY_ROOT]\n";
            return 0;
        }
        if (args.size() != 4 && args.size() != 5)
            throw Error("arguments", "Expected REPO BEFORE AFTER PATH [TEMPORARY_ROOT]");
        GitOptions git;
        if (args.size() == 5)
            git.temporary_root = fs::u8path(args[4]);
        const auto repo = GitRepository::open(fs::u8path(args[0]), git);
        const auto result = compare_git_binary_files(repo, args[1], args[2], args[3]);
        if (result.status() == "absent")
            throw Error("missing", "Both historical files are absent");
        const auto base = result.before.present
                              ? read_binary_file(result.before.file_path(), 256ull * 1024 * 1024)
                              : std::vector<std::uint8_t>{};
        const auto target = result.after.present
                                ? read_binary_file(result.after.file_path(), 256ull * 1024 * 1024)
                                : std::vector<std::uint8_t>{};
        const auto rebuilt = apply_binary_delta(base, BinaryDelta::parse(result.delta.to_json().dump()));
        if (rebuilt != target)
            throw Error("verification", "Historical reconstruction failed");
        std::cout << Json{{"summary", result.summary()}, {"verified", true}}.dump() << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 2;
    }
}
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
        args.push_back(fs::path(argv[i]).u8string());
    return run(args);
}
#else
int main(int argc, char **argv) { return run({argv + 1, argv + argc}); }
#endif
