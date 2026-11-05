#include <cyuediff/git.hpp>
#include <cyuediff/animation_pose.hpp>
#include <cyuediff/animation_socket_pose.hpp>
#include <charconv>
#include <iostream>
namespace {
const char *usage = "git_animation_pose REPO BEFORE_REV AFTER_REV BEFORE_ANIMATION AFTER_ANIMATION "
                    "BEFORE_SKELETON AFTER_SKELETON BEFORE_FRAME AFTER_FRAME [BEFORE_PROJECT AFTER_PROJECT]";
std::uint32_t frame(const std::string &text) {
    std::uint32_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw cyuediff::Error("arguments", "Source frames must be unsigned integers");
    return value;
}
cyuediff::Json fingerprints(const cyuediff::GitRevision &revision) {
    cyuediff::Json files = cyuediff::Json::object();
    for (const auto &file : revision.files) {
        const auto name = cyuediff::fs::u8path(file.path).filename();
        files[name.u8string()] = cyuediff::sha256_file(revision.asset_path().parent_path() / name);
    }
    return files;
}
int run(const std::vector<std::string> &args) {
    using namespace cyuediff;
    if (args.size() == 1 && args[0] == "--help") {
        std::cout << usage << '\n';
        return 0;
    }
    try {
        if (args.size() != 9 && args.size() != 11)
            throw Error("arguments", usage);
        const auto before_frame = frame(args[7]), after_frame = frame(args[8]);
        auto repository = GitRepository::open(fs::u8path(args[0]));
        GitRevision a, b, s, t;
        if (args.size() == 11) {
            GitProjectOptions options;
            options.discover_engine =
                false; // This example restricts mounts to historical repository content.
            const auto before = repository.project(args[1], args[9], options);
            const auto after = repository.project(args[2], args[10], options);
            a = repository.materialize(before, args[3]);
            b = repository.materialize(after, args[4]);
            s = repository.materialize(before, args[5]);
            t = repository.materialize(after, args[6]);
        } else {
            a = repository.materialize(args[1], args[3]);
            b = repository.materialize(args[2], args[4]);
            // Follow resolved animation commits, even when the supplied branch names move.
            s = repository.materialize(a.commit, args[5]);
            t = repository.materialize(b.commit, args[6]);
        }
        // Keep all four revisions alive while their packages and payloads are used.
        const auto before_animation = a.open(), before_skeleton = s.open(), after_animation = b.open(),
                   after_skeleton = t.open();
        const auto before = animation_pose(before_animation, before_skeleton, before_frame);
        const auto after = animation_pose(after_animation, after_skeleton, after_frame);
        const auto before_sockets = animation_socket_pose(before, before_skeleton),
                   after_sockets = animation_socket_pose(after, after_skeleton);
        const auto comparison = compare_animation_poses(before, after);
        std::cout << Json{{"beforeSocketOrigins", before_sockets.to_json()},
                          {"afterSocketOrigins", after_sockets.to_json()},
                          {"socketComparison", compare_animation_socket_poses(before_sockets, after_sockets)},
                          {"beforeRevision", a.to_json()},
                          {"afterRevision", b.to_json()},
                          {"beforeBinding", s.to_json()},
                          {"afterBinding", t.to_json()},
                          {"before", before.to_json()},
                          {"after", after.to_json()},
                          {"comparison", comparison},
                          {"fingerprints",
                           {{"beforeAnimation", fingerprints(a)},
                            {"afterAnimation", fingerprints(b)},
                            {"beforeSkeleton", fingerprints(s)},
                            {"afterSkeleton", fingerprints(t)}}}}
                         .dump(2)
                  << '\n';
        return comparison.value("complete", false) ? 0 : 2;
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
