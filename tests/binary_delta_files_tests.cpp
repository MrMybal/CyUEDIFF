#include <cyuediff/binary_delta.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
using namespace cyuediff;
int main() {
    int checks = 0;
    auto check = [&](bool good) {
        ++checks;
        if (!good)
            throw std::runtime_error("File generation check failed");
    };
    auto rejects = [&](auto fn) {
        bool failed = false;
        try {
            fn();
        } catch (const std::exception &) {
            failed = true;
        }
        check(failed);
    };
    const auto root =
        fs::current_path() / (".test-tmp-binary-files-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directory(root);
        const auto before = root / fs::u8path("before-é.bin"), after = root / "after.bin";
        auto write = [](const fs::path &p, const std::vector<std::uint8_t> &v) {
            std::ofstream out(p, std::ios::binary | std::ios::trunc);
            if (!v.empty())
                out.write(reinterpret_cast<const char *>(v.data()), static_cast<std::streamsize>(v.size()));
            if (!out)
                throw std::runtime_error("Fixture write failed");
        };
        BinaryDeltaOptions o;
        o.block_size = 16;
        std::mt19937 rng(169);
        std::vector<std::uint8_t> a, b;
        for (unsigned i = 0; i < 100; ++i) {
            a.resize(512 + rng() % 2048);
            for (auto &x : a)
                x = std::uint8_t(rng());
            b = a;
            b.insert(b.begin() + rng() % b.size(), {0, 255, 17});
            write(before, a);
            write(after, b);
            const auto d = make_binary_delta_files(before, after, o);
            check(d.to_json(o) == make_binary_delta(a, b, o).to_json(o));
            check(apply_binary_delta(a, d, o) == b);
        }
        check(apply_binary_delta({}, make_binary_delta_file_pair(std::nullopt, after, o), o) == b);
        check(make_binary_delta_file_pair(before, std::nullopt, o).target_bytes == 0);
        check(make_binary_delta_file_pair(std::nullopt, std::nullopt, o).operations.empty());
        write(after, a);
        auto limit = o;
        limit.max_source_read_bytes = 4 * a.size();
        check(make_binary_delta_files(before, after, limit).summary(limit)["identical"] == true);
        --limit.max_source_read_bytes;
        rejects([&] { make_binary_delta_files(before, after, limit); });
        limit = o;
        limit.max_input_bytes = 1;
        rejects([&] { make_binary_delta_files(before, after, limit); });
        rejects([&] { make_binary_delta_files(root / "missing", after, o); });
        rejects([&] { make_binary_delta_files(before, root, o); });
        rejects([&] { make_binary_delta_file_pair(fs::path{}, after, o); });
        bool changed = false;
        limit = o;
        b = a;
        b.push_back(19);
        write(after, b);
        limit.control.progress = [&](const OperationProgress &p) {
            if (!changed && p.phase == "Indexing binary base blocks") {
                changed = true;
                auto copy = a;
                copy[0] ^= 1;
                write(before, copy);
            }
        };
        rejects([&] { make_binary_delta_files(before, after, limit); });
        check(changed);
        write(before, a);
        changed = false;
        limit = o;
        limit.control.progress = [&](const OperationProgress &p) {
            if (!changed && p.phase == "Indexing binary base blocks") {
                changed = true;
                fs::resize_file(after, b.size() - 1);
            }
        };
        rejects([&] { make_binary_delta_files(before, after, limit); });
        check(changed);
        write(after, b);
        bool cancelled = false;
        limit = o;
        limit.control.cancelled = [&] { return cancelled; };
        limit.control.progress = [&](const OperationProgress &p) {
            if (p.phase == "Indexing binary base blocks")
                cancelled = true;
        };
        rejects([&] { make_binary_delta_files(before, after, limit); });
        check(cancelled);
        check(sha256_file(before) == sha256_bytes(a) && sha256_file(after) == sha256_bytes(b));
        write(before, {});
        write(after, {});
        check(make_binary_delta_files(before, after, o).operations.empty());
        fs::remove_all(root);
        std::cout << checks << " file-backed generation checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        fs::remove_all(root);
        std::cerr << e.what() << " after " << checks << " checks\n";
        return 1;
    }
}
