#include <cyuediff/binary_delta.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
using namespace cyuediff;
int main() {
    int checks = 0;
    auto check = [&](bool good) {
        ++checks;
        if (!good)
            throw std::runtime_error("Streaming delta check failed");
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
        fs::current_path() / (".test-tmp-binary-stream-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directory(root);
        const auto file = root / fs::u8path("base-é.bin");
        std::vector<std::uint8_t> base(2 * 1024 * 1024 + 123);
        std::uint32_t state = 165;
        for (auto &b : base) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            b = std::uint8_t(state);
        }
        auto write = [&](const auto &bytes) {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
            if (!out)
                throw std::runtime_error("Fixture write failed");
        };
        write(base);
        BinaryDeltaOptions o;
        std::vector<std::uint8_t> target(base.begin() + 1024 * 1024, base.end());
        target.insert(target.end(), {255, 0, 128});
        target.insert(target.end(), base.begin(), base.begin() + 1024 * 1024);
        BinaryDelta d;
        d.base_bytes = base.size();
        d.target_bytes = target.size();
        d.base_sha256 = sha256_bytes(base);
        d.target_sha256 = sha256_bytes(target);
        d.operations = {{BinaryDeltaKind::copy, 1024 * 1024, 1024 * 1024 + 123, {}},
                        {BinaryDeltaKind::literal, 0, 3, {255, 0, 128}},
                        {BinaryDeltaKind::copy, 0, 1024 * 1024, {}}};
        std::vector<std::uint8_t> output;
        std::size_t largest = 0, calls = 0;
        auto sink = [&](const std::uint8_t *bytes, std::size_t n) {
            ++calls;
            largest = std::max(largest, n);
            output.insert(output.end(), bytes, bytes + n);
        };
        apply_binary_delta_file(file, d, sink, o);
        check(output == target && calls == 4 && largest == 1024 * 1024);
        check(sha256_file(file) == d.base_sha256);
        check(sha256_bytes({}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        check(sha256_bytes({'a', 'b', 'c'}) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        output.clear();
        calls = 0;
        auto bad = d;
        bad.operations[0].offset = base.size();
        rejects([&] { apply_binary_delta_file(file, bad, sink, o); });
        check(calls == 0);
        bad = d;
        bad.base_sha256 = std::string(64, '0');
        rejects([&] { apply_binary_delta_file(file, bad, sink, o); });
        check(calls == 0);
        bad = d;
        ++bad.base_bytes;
        rejects([&] { apply_binary_delta_file(file, bad, sink, o); });
        check(calls == 0);
        rejects([&] { apply_binary_delta_file(file, d, {}, o); });
        rejects([&] { apply_binary_delta_file(root / "missing", d, sink, o); });
        check(calls == 0);
        bad = d;
        bad.target_sha256 = std::string(64, '0');
        rejects([&] { apply_binary_delta_file(file, bad, sink, o); });
        check(output ==
              target); // Provisional sink bytes are discarded by the host after a failed verification.
        bool marker = false;
        try {
            apply_binary_delta_file(
                file, d, [](const std::uint8_t *, std::size_t) { throw std::runtime_error("sink-error"); },
                o);
        } catch (const std::runtime_error &e) {
            marker = std::string(e.what()) == "sink-error";
        }
        check(marker);
        auto limit = o;
        limit.max_input_bytes = 1024;
        calls = 0;
        rejects([&] { apply_binary_delta_file(file, d, sink, limit); });
        check(calls == 0);
        bool cancelled = false;
        limit = o;
        limit.control.cancelled = [&] { return cancelled; };
        calls = 0;
        rejects([&] {
            apply_binary_delta_file(
                file, d,
                [&](const std::uint8_t *, std::size_t) {
                    ++calls;
                    cancelled = true;
                },
                limit);
        });
        check(calls == 1);
        calls = 0;
        rejects([&] {
            apply_binary_delta_file(
                file, d,
                [&](const std::uint8_t *, std::size_t) {
                    if (++calls == 1)
                        fs::resize_file(file, base.size() - 1);
                },
                o);
        });
        write(base);
        check(sha256_file(file) == d.base_sha256);
        const auto empty = make_binary_delta(base, {}, o);
        calls = 0;
        apply_binary_delta_file(file, empty, [&](const std::uint8_t *, std::size_t) { ++calls; }, o);
        check(calls == 0);
        std::vector<std::uint8_t> none;
        write(none);
        const auto insert = make_binary_delta({}, {0, 128, 255}, o);
        output.clear();
        apply_binary_delta_file(file, insert, sink, o);
        check(output == std::vector<std::uint8_t>({0, 128, 255}));
        fs::remove_all(root);
        std::cout << checks << " streaming reconstruction checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        fs::remove_all(root);
        std::cerr << e.what() << " after " << checks << " checks\n";
        return 1;
    }
}
