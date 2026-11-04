#include <cyuediff/git.hpp>
#include <iostream>
int run(const std::vector<std::string> &args) {
    if (args.size() != 2 && args.size() != 4 && args.size() != 5 && args.size() != 6 && args.size() != 8)
        return 2;
    try {
        auto repo = cyuediff::GitRepository::open(cyuediff::fs::u8path(args[0]));
        if (args.size() == 2) {
            std::cout << repo.history(args[1]).to_json().dump(2) << '\n';
            return 0;
        }
        cyuediff::GitComparison result;
        if (args.size() >= 6) {
            cyuediff::GitProjectOptions before_options, after_options;
            if (args.size() == 8) {
                before_options.engine_root = cyuediff::fs::u8path(args[6]);
                after_options.engine_root = cyuediff::fs::u8path(args[7]);
            }
            auto before = repo.project(args[1], args[4], before_options);
            auto after = repo.project(args[2], args[5], after_options);
            result = cyuediff::compare_git(repo, before, after, args[3]);
        } else
            result = cyuediff::compare_git(repo, args[1], args[2], args[3], args.size() == 5 ? args[4] : "");
        std::cout << result.to_json().dump(2) << '\n';
        auto status = result.status();
        return status == "identical" ? 0 : ((status == "incomplete" || status == "unverified") ? 2 : 1);
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 2;
    }
}
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
