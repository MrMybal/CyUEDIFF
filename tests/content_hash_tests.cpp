#include <cyuediff/content_hash.hpp>
#include <cyuediff/virtualized.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
using namespace cyuediff;
namespace {
int checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn, const char *code = nullptr) {
    try {
        fn();
    } catch (const Error &e) {
        check(!code || e.code == code, e.what());
        return;
    }
    throw std::runtime_error("Expected rejected payload");
}
void save(const fs::path &path, const std::vector<std::uint8_t> &bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out)
        throw std::runtime_error("Cannot write test file");
}
void big(std::vector<std::uint8_t> &b, std::size_t at, std::uint64_t n, unsigned count) {
    for (unsigned i = count; i > 0; --i)
        b.at(at++) = std::uint8_t(n >> ((i - 1) * 8));
}
void crc(std::vector<std::uint8_t> &b, std::size_t end) {
    std::uint32_t c = ~0u;
    for (std::size_t i = 8; i < end; ++i) {
        c ^= b[i];
        for (unsigned k = 0; k < 8; ++k)
            c = (c >> 1) ^ ((c & 1) ? 0xedb88320u : 0);
    }
    big(b, 4, ~c, 4);
}
std::vector<std::uint8_t> buffer(const std::vector<std::uint8_t> &raw, unsigned method = 0,
                                 bool compressed = false) {
    std::vector<std::uint8_t> b(64 + (method ? 4 : 0), 0);
    big(b, 0, 0xb7756362, 4);
    b[8] = std::uint8_t(method);
    b[11] = 10;
    big(b, 12, 1, 4);
    big(b, 16, raw.size(), 8);
    big(b, 24, b.size() + (compressed ? 1 : raw.size()), 8);
    const auto hash = blake3_bytes(raw);
    for (std::size_t i = 0; i < 32; ++i)
        b[32 + i] = std::uint8_t(std::stoul(hash.substr(i * 2, 2), nullptr, 16));
    if (method)
        big(b, 64, compressed ? 1 : raw.size(), 4);
    crc(b, b.size());
    if (compressed)
        b.push_back(42);
    else
        b.insert(b.end(), raw.begin(), raw.end());
    return b;
}
} // namespace
int main(int argc, char **argv) {
    const auto root =
        fs::current_path() / (".test-tmp-virtualized-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    try {
        if (argc != 2)
            throw std::runtime_error("Pass official BLAKE3 vector JSON");
        std::ifstream in(fs::u8path(argv[1]));
        const auto vectors = Json::parse(in);
        for (const auto &v : vectors.at("cases")) {
            std::vector<std::uint8_t> raw(v.at("input_len").get<std::size_t>());
            for (std::size_t i = 0; i < raw.size(); ++i)
                raw[i] = std::uint8_t(i % 251);
            const auto expected = v.at("hash").get<std::string>().substr(0, 64);
            check(blake3_bytes(raw) == expected, "Official BLAKE3 byte vector");
            save(root / "hash.bin", raw);
            check(blake3_file(root / "hash.bin") == expected, "Official BLAKE3 file vector");
        }
        std::vector<std::uint8_t> large(3 * 1024 * 1024 + 73, 0x38);
        save(root / "large.bin", large);
        check(blake3_file(root / "large.bin") == blake3_bytes(large), "File streaming across 1 MiB chunks");
        OperationControl cancelled;
        cancelled.cancelled = [] { return true; };
        rejects([&] { blake3_bytes({}, cancelled); }, "cancelled");
        rejects([&] { blake3_file(root / "hash.bin", cancelled); }, "cancelled");
        bool stop = false;
        OperationControl progress;
        progress.cancelled = [&] { return stop; };
        progress.progress = [&](const OperationProgress &p) {
            if (p.phase == "Hashing BLAKE3" && p.completed >= 1024 * 1024)
                stop = true;
        };
        rejects([&] { blake3_bytes(large, progress); }, "cancelled");
        stop = false;
        rejects([&] { blake3_file(root / "large.bin", progress); }, "cancelled");
        const std::vector<std::uint8_t> raw = {1, 2, 3, 4, 5, 6, 7, 8};
        const auto id = blake3_bytes(raw).substr(0, 40);
        const auto relative = virtualized_payload_relative_path(id);
        check(relative == (fs::path(id.substr(0, 2)) / id.substr(2, 2) / id.substr(4, 2) /
                           (id.substr(6) + ".upayload")),
              "Unreal sharded content path");
        for (const std::string bad : std::vector<std::string>{
                 "", std::string(40, '0'), std::string(40, 'G'), std::string(40, 'A'), std::string(39, '1'),
                 std::string(41, '1'), std::string(37, '1') + "/.."})
            rejects([&] { virtualized_payload_relative_path(bad); }, "virtualized-id");
        rejects([&] { virtualized_payload_file({}, id); }, "virtualized-root");
        rejects([&] { virtualized_payload_file(root, id); }, "virtualized-missing");
        const auto store = root / "store";
        const auto file = store / relative;
        const auto encoded = buffer(raw);
        save(file, encoded);
        BulkPayload p;
        p.storage = "virtualized";
        p.content_id = id;
        p.raw_bytes = raw.size();
        BulkDecodeOptions o;
        o.virtualized_payload_root = store;
        check(virtualized_payload_file(store, id) == fs::canonical(file),
              "Explicit existing filesystem backend");
        check(decode_bulk_payload(p, o) == raw, "Virtualized payload decoded and hash verified");
        o.max_payload_bytes = raw.size() - 1;
        rejects([&] { decode_bulk_payload(p, o); }, "limit");
        o.max_payload_bytes = encoded.size() - 1;
        rejects([&] { decode_bulk_payload(p, o); }, "limit");
        o.max_payload_bytes = 1024;
        auto bad = encoded;
        bad.back() ^= 1;
        save(file, bad);
        rejects([&] { decode_bulk_payload(p, o); }, "bulk-hash");
        bad = encoded;
        bad[63] ^= 1;
        crc(bad, 64);
        save(file, bad);
        rejects([&] { decode_bulk_payload(p, o); }, "bulk-hash"); // full 32 bytes, not just FIoHash
        bad = encoded;
        bad[32] ^= 1;
        crc(bad, 64);
        save(file, bad);
        rejects([&] { decode_bulk_payload(p, o); }, "texture-buffer");
        bad = encoded;
        bad[4] ^= 1;
        save(file, bad);
        rejects([&] { decode_bulk_payload(p, o); }, "texture-buffer-crc");
        bad = encoded;
        bad.push_back(0);
        save(file, bad);
        rejects([&] { decode_bulk_payload(p, o); }, "texture-buffer");
        for (std::size_t n = 0; n < encoded.size(); ++n) {
            save(file, {encoded.begin(), encoded.begin() + n});
            rejects([&] { decode_bulk_payload(p, o); });
        }
        for (unsigned method : {3u, 4u}) {
            save(file, buffer(raw, method));
            check(decode_bulk_payload(p, o) == raw, "Raw-sized compressed-buffer blocks need no codec");
            save(file, buffer(raw, method, true));
            rejects([&] { decode_bulk_payload(p, o); }, "texture-codec");
            o.decoder = [&](std::uint32_t m, const std::uint8_t *src, std::size_t size, std::uint8_t *dst,
                            std::size_t count) {
                check(m == method && size == 1 && src[0] == 42 && count == raw.size(),
                      "Explicit codec bounded arguments");
                std::copy(raw.begin(), raw.end(), dst);
                return count;
            };
            check(decode_bulk_payload(p, o) == raw, "Compressed virtualized block verified after decode");
            o.decoder = [](std::uint32_t, const std::uint8_t *, std::size_t, std::uint8_t *dst,
                           std::size_t count) {
                std::fill_n(dst, count, 0);
                return count;
            };
            rejects([&] { decode_bulk_payload(p, o); }, "bulk-hash");
            o.decoder = {};
        }
        save(file, encoded);
        o.control = cancelled;
        rejects([&] { decode_bulk_payload(p, o); }, "cancelled");
        o.control = {};
        TextureSource texture;
        texture.width = 2;
        texture.height = 1;
        texture.mips = 1;
        texture.slices = 1;
        texture.layers = 1;
        texture.block_offsets = {0};
        texture.format = "TSF_BGRA8";
        texture.payload = p;
        TextureDecodeOptions image;
        image.virtualized_payload_root = store;
        const auto decoded = decode_texture(texture, 0, image);
        check(decoded.width == 2 && decoded.height == 1 &&
                  decoded.pixels == std::vector<std::uint8_t>({3, 2, 1, 4, 7, 6, 5, 8}),
              "Virtualized source texture through public image decoder");
        // A link to a directory outside the selected root must not expand its scope.
        const auto links = root / "links";
        fs::create_directories(links);
        std::error_code ec;
        fs::create_directory_symlink(store / id.substr(0, 2), links / id.substr(0, 2), ec);
        if (!ec)
            rejects([&] { virtualized_payload_file(links, id); }, "virtualized-path");
        std::cout << checks << " content hash and virtualized payload checks passed\n";
        if (root.parent_path() != fs::current_path() ||
            root.filename().u8string().find(".test-tmp-virtualized-") != 0)
            throw std::runtime_error("Invalid cleanup root");
        fs::remove_all(root);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\nFixtures: " << root << '\n';
        return 1;
    }
}
