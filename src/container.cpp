#include <cyuediff/container.hpp>
#include <cyuediff/content_hash.hpp>
#include "archive.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <set>
namespace cyuediff {
namespace {
using Bytes = std::vector<std::uint8_t>;
constexpr std::uint32_t invalid = ~std::uint32_t(0);
void require(bool condition, const char *message) {
    if (!condition)
        throw Error("container-format", message);
}
void extent(std::uint64_t offset, std::uint64_t size, std::uint64_t end) {
    if (offset > end || size > end - offset)
        throw Error("bounds", "Container range exceeds its source", offset);
}
std::string hex(const std::uint8_t *p, std::size_t n) {
    static const char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        result += digits[p[i] >> 4];
        result += digits[p[i] & 15];
    }
    return result;
}
// SHA-1 is required by Pak. It detects corruption, not authenticity.
std::string sha1(const Bytes &data, const OperationControl &control) {
    std::uint32_t state[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
    auto rotate = [](std::uint32_t x, unsigned n) { return (x << n) | (x >> (32 - n)); };
    auto padded = (static_cast<std::uint64_t>(data.size()) + 9 + 63) & ~63ull;
    for (std::uint64_t pos = 0; pos < padded; pos += 64) {
        if ((pos & ((1u << 20) - 1)) == 0)
            control.check();
        std::uint32_t w[80]{};
        for (unsigned j = 0; j < 64; ++j) {
            auto at = pos + j;
            std::uint8_t b =
                at < data.size() ? data[static_cast<std::size_t>(at)] : (at == data.size() ? 128 : 0);
            if (at >= padded - 8)
                b = static_cast<std::uint8_t>((static_cast<std::uint64_t>(data.size()) * 8) >>
                                              ((padded - 1 - at) * 8));
            w[j / 4] |= std::uint32_t(b) << (24 - 8 * (j % 4));
        }
        for (unsigned j = 16; j < 80; ++j)
            w[j] = rotate(w[j - 3] ^ w[j - 8] ^ w[j - 14] ^ w[j - 16], 1);
        auto a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
        for (unsigned j = 0; j < 80; ++j) {
            auto f = j < 20 ? ((b & c) | (~b & d))
                            : (j < 40 ? (b ^ c ^ d) : (j < 60 ? ((b & c) | (b & d) | (c & d)) : (b ^ c ^ d)));
            auto k = j < 20 ? 0x5a827999u : (j < 40 ? 0x6ed9eba1u : (j < 60 ? 0x8f1bbcdcu : 0xca62c1d6u));
            auto t = rotate(a, 5) + f + e + k + w[j];
            e = d;
            d = c;
            c = rotate(b, 30);
            b = a;
            a = t;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
    }
    std::uint8_t result[20];
    for (unsigned j = 0; j < 20; ++j)
        result[j] = static_cast<std::uint8_t>(state[j / 4] >> (24 - 8 * (j % 4)));
    return hex(result, 20);
}
struct Cursor {
    const Bytes &bytes;
    const ContainerOptions &options;
    std::size_t pos = 0;
    void range(std::uint64_t n) const {
        extent(pos, n, bytes.size());
        options.control.check();
    }
    std::uint64_t number(unsigned n, bool big = false) {
        range(n);
        std::uint64_t v = 0;
        for (unsigned j = 0; j < n; ++j)
            v |= std::uint64_t(bytes[pos++]) << (8 * (big ? n - 1 - j : j));
        return v;
    }
    std::uint32_t u32() {
        return static_cast<std::uint32_t>(number(4));
    }
    std::uint64_t u64() {
        return number(8);
    }
    std::uint32_t count(std::uint64_t limit) {
        auto n = u32();
        if (n > limit)
            throw Error("container-limit", "Container count exceeds configured limit");
        return n;
    }
    bool boolean() {
        auto v = u32();
        require(v <= 1, "Invalid serialized boolean");
        return v != 0;
    }
    void skip(std::uint64_t n) {
        range(n);
        pos += static_cast<std::size_t>(n);
    }
    std::string hash(unsigned n) {
        range(n);
        auto s = hex(bytes.data() + pos, n);
        pos += n;
        return s;
    }
    std::string fixed(unsigned n) {
        range(n);
        auto start = pos;
        skip(n);
        auto end = std::find(bytes.begin() + start, bytes.begin() + pos, 0);
        require(end != bytes.begin() + pos, "Unterminated compression method");
        return std::string(bytes.begin() + start, end);
    }
    std::string string() {
        auto len = static_cast<std::int32_t>(u32());
        if (!len)
            return {};
        auto count = len < 0 ? -static_cast<std::int64_t>(len) : static_cast<std::int64_t>(len);
        auto byte_count = static_cast<std::uint64_t>(count) * (len < 0 ? 2 : 1);
        if (byte_count > options.max_string_bytes)
            throw Error("container-limit", "Container string exceeds configured limit");
        range(byte_count);
        std::string result;
        if (len > 0) {
            require(bytes[pos + count - 1] == 0, "Unterminated container string");
            result.assign(reinterpret_cast<const char *>(bytes.data() + pos),
                          static_cast<std::size_t>(count - 1));
            skip(byte_count);
            require(result.find('\0') == std::string::npos, "Embedded null in container string");
            return result;
        }
        auto append = [&](std::uint32_t cp) {
            if (cp < 128)
                result += static_cast<char>(cp);
            else if (cp < 2048) {
                result += static_cast<char>(192 | (cp >> 6));
                result += static_cast<char>(128 | (cp & 63));
            } else if (cp < 65536) {
                result += static_cast<char>(224 | (cp >> 12));
                result += static_cast<char>(128 | ((cp >> 6) & 63));
                result += static_cast<char>(128 | (cp & 63));
            } else {
                result += static_cast<char>(240 | (cp >> 18));
                result += static_cast<char>(128 | ((cp >> 12) & 63));
                result += static_cast<char>(128 | ((cp >> 6) & 63));
                result += static_cast<char>(128 | (cp & 63));
            }
        };
        for (std::int64_t j = 0; j < count; ++j) {
            auto cp = static_cast<std::uint32_t>(number(2));
            if (j == count - 1) {
                require(cp == 0, "Unterminated UTF-16 container string");
                break;
            }
            require(cp != 0, "Embedded null in container string");
            if (cp >= 0xd800 && cp <= 0xdbff) {
                require(j + 2 < count, "Truncated UTF-16 surrogate");
                auto lo = number(2);
                ++j;
                require(lo >= 0xdc00 && lo <= 0xdfff, "Invalid UTF-16 surrogate");
                cp = 65536 + ((cp - 0xd800) << 10) + static_cast<std::uint32_t>(lo - 0xdc00);
            } else
                require(cp < 0xdc00 || cp > 0xdfff, "Invalid UTF-16 surrogate");
            append(cp);
        }
        return result;
    }
};
Bytes read_range(const fs::path &path, std::uint64_t offset, std::uint64_t size, std::uint64_t limit,
                 const OperationControl &control) {
    control.check();
    if (size > limit || size > std::numeric_limits<std::size_t>::max())
        throw Error("container-limit", "Container read exceeds configured limit");
    ReadOptions options;
    options.control = control;
    detail::Archive archive(path, options);
    extent(offset, size, archive.size());
    archive.seek(offset);
    Bytes result(static_cast<std::size_t>(size));
    archive.read(result.data(), result.size());
    return result;
}
void safe_component(const std::string &s) {
    require(!s.empty() && s != "." && s != ".." && s.find_first_of("/\\:") == std::string::npos,
            "Invalid container path component");
}
void safe_path(const std::string &s, const ContainerOptions &o) {
    require(!s.empty() && s.size() <= o.max_string_bytes, "Invalid container member path");
    std::size_t at = 0;
    unsigned depth = 0;
    while (at < s.size()) {
        auto end = s.find('/', at);
        if (end == std::string::npos)
            end = s.size();
        safe_component(s.substr(at, end - at));
        if (++depth > o.max_depth)
            throw Error("container-limit", "Container directory depth exceeds configured limit");
        at = end + 1;
    }
}

struct Block {
    std::uint64_t offset = 0;
    std::uint32_t stored = 0, raw = 0, method = 0;
};
struct PakEntry {
    std::uint64_t offset = 0, stored = 0, raw = 0;
    std::uint32_t method = 0, block_size = 0;
    std::uint8_t flags = 0;
    std::string hash;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> blocks;
    std::uint64_t header_size() const {
        return 53 + (method ? 4 + 16 * blocks.size() : 0);
    }
};
PakEntry pak_entry(Cursor &c) {
    PakEntry e;
    e.offset = c.u64();
    e.stored = c.u64();
    e.raw = c.u64();
    e.method = c.u32();
    e.hash = c.hash(20);
    if (e.method) {
        auto n = c.count(c.options.max_blocks);
        c.range(std::uint64_t(n) * 16);
        e.blocks.reserve(n);
        for (unsigned j = 0; j < n; ++j) {
            auto begin = c.u64(), end = c.u64();
            e.blocks.emplace_back(begin, end);
        }
    }
    e.flags = static_cast<std::uint8_t>(c.number(1));
    e.block_size = c.u32();
    return e;
}
PakEntry encoded_entry(Cursor &c) {
    PakEntry e;
    auto bits = c.u32();
    auto block_size = (bits & 63) == 63 ? c.u32() : (bits & 63) << 11;
    e.method = (bits >> 23) & 63;
    e.flags = (bits & (1u << 22)) ? 1 : 0;
    e.offset = (bits & (1u << 31)) ? c.u32() : c.u64();
    e.raw = (bits & (1u << 30)) ? c.u32() : c.u64();
    e.stored = e.method ? ((bits & (1u << 29)) ? c.u32() : c.u64()) : e.raw;
    auto n = (bits >> 6) & 65535;
    if (n > c.options.max_blocks)
        throw Error("container-limit", "Too many Pak compression blocks");
    if (n) {
        require(n != 1 || e.raw <= invalid, "Oversized single Pak block");
        e.block_size = n == 1 ? static_cast<std::uint32_t>(e.raw) : block_size;
    }
    e.blocks.resize(n);
    auto at = e.header_size();
    for (unsigned j = 0; j < n; ++j) {
        auto size = n == 1 && !(e.flags & 1) ? e.stored : c.u32();
        extent(at, size, std::numeric_limits<std::uint64_t>::max());
        e.blocks[j] = {at, at + size};
        at += (e.flags & 1) ? (size + 15) & ~15ull : size;
    }
    return e;
}
void decode(const std::string &method, const Bytes &input, Bytes &out, const ContainerReadOptions &options) {
    options.control.check();
    if (!options.decoder)
        throw Error("container-codec", "Container block requires codec " + method);
    if (options.decoder(method, input.data(), input.size(), out.data(), out.size()) != out.size())
        throw Error("container-codec", "Container codec returned an invalid decoded size");
    options.control.check();
}
} // namespace
struct Container::Impl {
    std::string kind, mount;
    unsigned version = 0;
    std::vector<ContainerEntry> entries;
    std::vector<fs::path> files;
    std::vector<std::uint64_t> file_sizes;
    std::vector<std::string> methods{"None"}, diagnostics;
    std::vector<Block> blocks;
    std::vector<PakEntry> pak_entries;
    std::uint32_t block_size = 0;
    std::uint64_t partition_size = 0, data_end = 0;
    ContainerOptions limits;
    void io_open(const Bytes &bytes, const ContainerOptions &options);
    void pak_open(const ContainerOptions &options);
};
void Container::Impl::io_open(const Bytes &bytes, const ContainerOptions &options) {
    kind = "iostore";
    Cursor c{bytes, options};
    require(bytes.size() >= 144 && std::memcmp(bytes.data(), "-==--==--==--==-", 16) == 0,
            "Invalid IoStore magic");
    c.pos = 16;
    version = static_cast<unsigned>(c.number(1));
    if (version < 3 || version > 8)
        throw Error("container-version", "Supported IoStore TOC versions are 3 through 8");
    c.skip(3);
    require(c.u32() == 144, "Unexpected IoStore header size");
    auto n = c.count(options.max_entries), nb = c.count(options.max_blocks);
    require(c.u32() == 12, "Unexpected IoStore block entry size");
    auto nm = c.count(255), ml = c.count(256);
    block_size = c.u32();
    auto directory_size = c.u32(), partitions = c.count(4096);
    c.skip(24);
    auto flags = c.number(1);
    c.skip(3);
    auto seeds = c.count(options.max_entries);
    partition_size = c.u64();
    auto overflow = c.count(options.max_entries);
    c.skip(44);
    if (flags & 2)
        throw Error("container-encrypted", "Encrypted IoStore containers are not supported");
    if (flags & 16)
        throw Error("container-ondemand", "On-demand IoStore storage is not supported");
    require((flags & ~15ull) == 0 && block_size && partitions && partition_size,
            "Invalid IoStore header fields");
    require(version >= 4 || seeds == 0, "Unexpected perfect hash seeds");
    require(version >= 5 || overflow == 0, "Unexpected perfect hash overflow");
    c.range(std::uint64_t(n) * 22 + (std::uint64_t(seeds) + overflow) * 4 + std::uint64_t(nb) * 12 +
            std::uint64_t(nm) * ml);
    entries.resize(n);
    std::set<std::string> ids;
    for (auto &e : entries) {
        e.id = c.hash(12);
        e.chunk_type = bytes[c.pos - 1];
        require(ids.insert(e.id).second, "Duplicate IoStore chunk ID");
    }
    for (auto &e : entries) {
        e.offset = c.number(5, true);
        e.size = c.number(5, true);
    }
    c.skip((std::uint64_t(seeds) + overflow) * 4);
    blocks.reserve(nb);
    for (unsigned j = 0; j < nb; ++j) {
        Block b;
        b.offset = c.number(5);
        b.stored = static_cast<std::uint32_t>(c.number(3));
        b.raw = static_cast<std::uint32_t>(c.number(3));
        b.method = static_cast<std::uint32_t>(c.number(1));
        require(b.stored && b.raw && b.raw <= block_size && b.method <= nm && (b.method || b.stored == b.raw),
                "Invalid IoStore compression block");
        blocks.push_back(b);
    }
    for (unsigned j = 0; j < nm; ++j) {
        auto name = c.fixed(ml);
        require(!name.empty(), "Empty compression method");
        methods.push_back(name);
    }
    if (flags & 4) {
        auto sig = c.count(16384);
        c.skip(std::uint64_t(sig) * 2 + std::uint64_t(nb) * 20);
        diagnostics.push_back("RSA signatures are present but are not authenticated by this reader.");
    }
    auto directory_start = c.pos;
    c.skip(directory_size);
    c.range(std::uint64_t(n) * (version >= 8 ? 24 : 33));
    for (auto &entry : entries) {
        options.control.check();
        entry.source_hash = c.hash(20);
        // Version 8 replaces the legacy 32-byte record with FIoHash + flags + pad.
        if (version < 8)
            c.skip(12);
        entry.storage_flags = static_cast<std::uint8_t>(c.number(1));
        if (version >= 8)
            c.skip(3);
        require((entry.storage_flags & ~3u) == 0, "Unsupported IoStore chunk storage flags");
    }
    require(c.pos == bytes.size(), "Unexpected trailing IoStore TOC data");
    diagnostics.push_back("Stored IoStore BLAKE3-160 source hashes are checked over complete decoded "
                          "chunks on read. This does not authenticate the TOC or RSA signatures.");
    for (unsigned j = 0; j < partitions; ++j) {
        auto base = files.front();
        base.replace_extension();
        auto path = fs::u8path(base.u8string() + (j ? "_s" + std::to_string(j) : "") + ".ucas");
        std::error_code ec;
        auto size = fs::file_size(path, ec);
        if (ec)
            throw Error("container-partition", "Missing or inaccessible UCAS partition");
        files.push_back(path);
        file_sizes.push_back(size);
    }
    std::vector<std::uint64_t> prefix(1, 0);
    for (const auto &b : blocks) {
        auto part = b.offset / partition_size;
        require(part < partitions, "IoStore block references an invalid partition");
        auto at = b.offset % partition_size;
        extent(at, b.stored, partition_size);
        extent(at, b.stored, file_sizes[static_cast<std::size_t>(part) + 1]);
        prefix.push_back(prefix.back() + b.stored);
    }
    for (auto &e : entries) {
        extent(e.offset, e.size, std::uint64_t(nb) * block_size);
        if (!e.size)
            continue;
        auto first = e.offset / block_size, last = (e.offset + e.size - 1) / block_size;
        require((e.offset + e.size - 1) % block_size < blocks[static_cast<std::size_t>(last)].raw,
                "Chunk exceeds final decoded block");
        e.blocks = static_cast<std::uint32_t>(last - first + 1);
        e.stored_bytes = prefix[static_cast<std::size_t>(last + 1)] - prefix[static_cast<std::size_t>(first)];
    }
    if (directory_size) {
        require((flags & 8) != 0, "Directory index without indexed flag");
        Bytes directory(bytes.begin() + directory_start, bytes.begin() + directory_start + directory_size);
        Cursor d{directory, options};
        mount = d.string();
        struct Dir {
            std::uint32_t name, child, sibling, file;
        };
        struct File {
            std::uint32_t name, next, chunk;
        };
        auto nd = d.count(options.max_entries);
        d.range(std::uint64_t(nd) * 16);
        std::vector<Dir> dirs;
        for (unsigned j = 0; j < nd; ++j)
            dirs.push_back({d.u32(), d.u32(), d.u32(), d.u32()});
        auto nf = d.count(options.max_entries);
        d.range(std::uint64_t(nf) * 12);
        std::vector<File> members;
        for (unsigned j = 0; j < nf; ++j)
            members.push_back({d.u32(), d.u32(), d.u32()});
        auto ns = d.count(options.max_entries);
        std::vector<std::string> strings;
        for (unsigned j = 0; j < ns; ++j)
            strings.push_back(d.string());
        require(d.pos == directory.size() && nd > 0 && dirs[0].name == invalid && dirs[0].sibling == invalid,
                "Invalid directory index root or trailing data");
        std::vector<bool> visited_d(nd), visited_f(nf);
        std::set<std::string> paths;
        struct Pending {
            std::uint32_t index, depth;
            std::string parent;
        };
        std::vector<Pending> pending{{0, 0, {}}};
        while (!pending.empty()) {
            options.control.check();
            auto p = std::move(pending.back());
            pending.pop_back();
            if (p.depth > options.max_depth)
                throw Error("container-limit", "IoStore directory depth exceeds limit");
            require(p.index < nd && !visited_d[p.index], "Invalid or cyclic IoStore directory");
            visited_d[p.index] = true;
            const auto &dir = dirs[p.index];
            auto path = p.parent;
            if (p.index) {
                require(dir.name < ns, "Invalid directory name index");
                safe_component(strings[dir.name]);
                path += strings[dir.name] + "/";
            }
            if (dir.sibling != invalid)
                pending.push_back({dir.sibling, p.depth, p.parent});
            if (dir.child != invalid)
                pending.push_back({dir.child, p.depth + 1, path});
            for (auto f = dir.file; f != invalid; f = members[f].next) {
                require(f < nf && !visited_f[f], "Invalid or cyclic IoStore file list");
                visited_f[f] = true;
                const auto &file = members[f];
                require(file.name < ns && file.chunk < n, "Invalid IoStore file entry");
                safe_component(strings[file.name]);
                auto name = path + strings[file.name];
                safe_path(name, options);
                require(paths.insert(name).second && entries[file.chunk].path.empty(),
                        "Duplicate IoStore filename or alias");
                entries[file.chunk].path = std::move(name);
            }
        }
        require(std::find(visited_d.begin(), visited_d.end(), false) == visited_d.end() &&
                    std::find(visited_f.begin(), visited_f.end(), false) == visited_f.end(),
                "Unreachable IoStore directory entries");
    }
}

void Container::Impl::pak_open(const ContainerOptions &options) {
    kind = "pak";
    auto size = file_sizes.front();
    if (size < 221)
        throw Error("container-format", "Truncated Pak footer");
    auto footer = read_range(files.front(), size - 221, 221, 221, options.control);
    Cursor f{footer, options};
    f.skip(16);
    auto encrypted = f.number(1);
    require(f.u32() == 0x5a6f12e1, "Invalid or unsupported Pak footer");
    version = f.u32();
    if (version != 10 && version != 11)
        throw Error("container-version", "Supported Pak versions are 10 and 11");
    if (encrypted)
        throw Error("container-encrypted", "Encrypted Pak indexes are not supported");
    auto index_offset = f.u64(), index_size = f.u64();
    auto index_hash = f.hash(20);
    data_end = size - 221;
    for (unsigned j = 0; j < 5; ++j)
        methods.push_back(f.fixed(32));
    auto index = [&](std::uint64_t at, std::uint64_t len, const std::string &hash) {
        extent(at, len, data_end);
        auto b = read_range(files.front(), at, len, options.max_index_bytes, options.control);
        if (sha1(b, options.control) != hash)
            throw Error("container-integrity", "Pak index SHA-1 mismatch");
        return b;
    };
    auto primary = index(index_offset, index_size, index_hash);
    Cursor c{primary, options};
    mount = c.string();
    auto n = c.count(options.max_entries);
    c.skip(8);
    struct Index {
        bool present = false;
        std::uint64_t offset = 0, size = 0;
        std::string hash;
    };
    auto descriptor = [&] {
        Index i;
        i.present = c.boolean();
        if (i.present) {
            i.offset = c.u64();
            i.size = c.u64();
            i.hash = c.hash(20);
        }
        return i;
    };
    auto path_hash = descriptor(), full = descriptor();
    if (!full.present || full.offset == ~0ull)
        throw Error("container-index",
                    "Pak requires a full directory index; path-hash-only indexes are not supported");
    auto encoded_size = c.count(options.max_index_bytes);
    c.range(encoded_size);
    Bytes encoded(primary.begin() + c.pos, primary.begin() + c.pos + encoded_size);
    c.skip(encoded_size);
    auto nonencoded = c.count(options.max_entries);
    std::vector<PakEntry> regular;
    for (unsigned j = 0; j < nonencoded; ++j)
        regular.push_back(pak_entry(c));
    require(c.pos == primary.size(), "Unexpected trailing Pak primary index data");
    if (full.size > options.max_index_bytes - primary.size())
        throw Error("container-limit", "Aggregate Pak index size exceeds limit");
    auto directory = index(full.offset, full.size, full.hash);
    require(index_offset >= full.offset + full.size || full.offset >= index_offset + index_size,
            "Overlapping Pak indexes");
    data_end = std::min(index_offset, full.offset);
    if (path_hash.present && path_hash.offset != ~0ull) {
        extent(path_hash.offset, path_hash.size, size - 221);
        data_end = std::min(data_end, path_hash.offset);
    }
    Cursor d{directory, options};
    auto dirs = d.count(options.max_entries);
    std::set<std::string> paths;
    std::uint64_t total_blocks = 0;
    for (unsigned j = 0; j < dirs; ++j) {
        auto prefix = d.string();
        if (prefix == "/")
            prefix.clear();
        if (!prefix.empty()) {
            require(prefix.back() == '/', "Invalid Pak directory suffix");
            safe_path(prefix, options);
        }
        auto nf = d.count(n - entries.size());
        for (unsigned k = 0; k < nf; ++k) {
            auto name = d.string();
            safe_component(name);
            auto location = static_cast<std::int32_t>(d.u32());
            ContainerEntry e;
            e.path = prefix + name;
            safe_path(e.path, options);
            e.id = e.path;
            require(paths.insert(e.path).second, "Duplicate Pak member path");
            PakEntry pe;
            if (location == std::numeric_limits<std::int32_t>::min()) {
                e.deleted = true;
            } else if (location >= 0) {
                require(location != std::numeric_limits<std::int32_t>::max(), "Invalid Pak entry location");
                Cursor ec{encoded, options};
                ec.pos = static_cast<std::size_t>(location);
                pe = encoded_entry(ec);
            } else {
                auto at = static_cast<std::uint32_t>(-(location + 1));
                require(at < regular.size(), "Invalid Pak nonencoded entry index");
                pe = regular[at];
            }
            e.deleted = e.deleted || (pe.flags & 2);
            e.encrypted = (pe.flags & 1) != 0;
            if (!e.deleted) {
                require(pe.method < methods.size() && !methods[pe.method].empty() && !(pe.flags & ~3),
                        "Invalid Pak method or flags");
                require(pe.method ? (!pe.blocks.empty() && pe.block_size)
                                  : (pe.blocks.empty() && pe.raw == pe.stored),
                        "Invalid Pak compression layout");
                extent(pe.offset, pe.header_size(), data_end);
                extent(pe.offset + pe.header_size(), pe.stored, data_end);
                if (pe.method) {
                    require((pe.raw ? (pe.raw - 1) / pe.block_size + 1 : 0) == pe.blocks.size(),
                            "Invalid Pak compression block count");
                    auto at = pe.header_size();
                    for (const auto &b : pe.blocks) {
                        require(b.first == at && b.second > b.first, "Noncontiguous or invalid Pak block");
                        extent(b.first - pe.header_size(), b.second - b.first, pe.stored);
                        at = e.encrypted ? (b.second + 15) & ~15ull : b.second;
                    }
                    require(e.encrypted || at - pe.header_size() == pe.stored,
                            "Invalid Pak stored block total");
                }
            }
            total_blocks += pe.blocks.size();
            if (total_blocks > options.max_blocks)
                throw Error("container-limit", "Aggregate Pak compression blocks exceed limit");
            e.offset = pe.offset;
            e.size = pe.raw;
            e.stored_bytes = pe.stored;
            e.blocks = static_cast<std::uint32_t>(pe.blocks.size());
            entries.push_back(std::move(e));
            pak_entries.push_back(std::move(pe));
        }
    }
    require(d.pos == directory.size() && entries.size() == n,
            "Pak directory entry count mismatch or trailing data");
    diagnostics.push_back("Primary and full directory index SHA-1 verified. Member stored-byte SHA-1 is "
                          "verified on read; signatures are not authenticated.");
}
Container::Container(std::shared_ptr<const Impl> impl) : impl_(std::move(impl)) {}
Container Container::open(const fs::path &path, const ContainerOptions &options) {
    options.control.check();
    auto impl = std::make_shared<Impl>();
    impl->limits = options;
    impl->limits.control = {};
    impl->files.push_back(fs::absolute(path));
    std::error_code ec;
    auto size = fs::file_size(path, ec);
    if (ec)
        throw Error("io", "Cannot open container");
    impl->file_sizes.push_back(size);
    auto extension = path.extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension == ".utoc")
        impl->io_open(read_range(path, 0, size, options.max_index_bytes, options.control), options);
    else if (extension == ".pak")
        impl->pak_open(options);
    else
        throw Error("container-format", "Expected a .pak or .utoc input");
    options.control.checkpoint("container-index", impl->entries.size(), impl->entries.size());
    return Container(std::move(impl));
}
const std::vector<ContainerEntry> &Container::entries() const {
    return impl_->entries;
}
const std::vector<fs::path> &Container::files() const {
    return impl_->files;
}
Json ContainerEntry::to_json() const {
    Json result = {{"id", id},
                   {"path", path},
                   {"offset", offset},
                   {"size", size},
                   {"storedBytes", stored_bytes},
                   {"blocks", blocks},
                   {"chunkType", chunk_type},
                   {"encrypted", encrypted},
                   {"deleted", deleted}};
    if (!source_hash.empty()) {
        result["sourceHash"] = {{"algorithm", "blake3-160"}, {"expected", source_hash}};
        result["storageFlags"] = storage_flags;
    }
    return result;
}
Json Container::metadata() const {
    Json paths = Json::array();
    for (const auto &p : impl_->files)
        paths.push_back(p.u8string());
    return {{"schema", "cyuediff.container/1"},
            {"kind", impl_->kind},
            {"version", impl_->version},
            {"mountPoint", impl_->mount},
            {"files", paths},
            {"compressionMethods", impl_->methods},
            {"entryCount", impl_->entries.size()},
            {"memberIntegrity",
             {{"algorithm", impl_->kind == "iostore" ? "blake3-160" : "sha1"},
              {"scope", impl_->kind == "iostore" ? "decoded-chunk" : "stored-member"},
              {"verification", "required-on-read"},
              {"signaturesAuthenticated", false}}},
            {"diagnostics", impl_->diagnostics},
            {"coverage", "container-bytes"}};
}
Json Container::to_json() const {
    auto result = metadata();
    result.erase("entryCount");
    auto &entries = result["entries"] = Json::array();
    for (const auto &entry : impl_->entries)
        entries.push_back(entry.to_json());
    return result;
}
ContainerBlockDecoder container_block_decoder(BlockDecoder decoder) {
    return [decoder = std::move(decoder)](const std::string &name, const std::uint8_t *src, std::size_t n,
                                          std::uint8_t *dst, std::size_t size) {
        auto method = name;
        std::transform(method.begin(), method.end(), method.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!decoder || (method != "oodle" && method != "lz4"))
            throw Error("container-codec", "Unsupported container codec " + name);
        return decoder(method == "oodle" ? 3u : 4u, src, n, dst, size);
    };
}
std::vector<std::uint8_t> Container::read(std::size_t index, const ContainerReadOptions &options) const {
    options.control.check();
    const auto &i = *impl_;
    if (index >= i.entries.size())
        throw Error("container-entry", "Container entry index is out of range");
    const auto &e = i.entries[index];
    if (e.deleted)
        throw Error("container-deleted", "Cannot read a deleted Pak member");
    if (e.encrypted)
        throw Error("container-encrypted", "Encrypted Pak members are not supported");
    if (e.size > options.max_payload_bytes || e.size > std::numeric_limits<std::size_t>::max())
        throw Error("container-limit", "Decoded member exceeds configured limit");
    for (std::size_t j = 0; j < i.files.size(); ++j) {
        std::error_code ec;
        auto size = fs::file_size(i.files[j], ec);
        if (ec || size != i.file_sizes[j])
            throw Error("container-changed", "Container source size changed after indexing");
    }
    Bytes result(static_cast<std::size_t>(e.size));
    if (i.kind == "iostore") {
        std::uint64_t done = 0;
        while (done < e.size) {
            auto virtual_at = e.offset + done;
            auto bi = virtual_at / i.block_size;
            const auto &b = i.blocks[static_cast<std::size_t>(bi)];
            auto within = virtual_at % i.block_size;
            require(within < b.raw, "Chunk references padding after a decoded block");
            if (b.raw > options.max_block_bytes || b.stored > options.max_block_bytes)
                throw Error("container-limit", "IoStore block exceeds configured limit");
            auto part = b.offset / i.partition_size;
            auto input = read_range(i.files[static_cast<std::size_t>(part) + 1], b.offset % i.partition_size,
                                    b.stored, options.max_block_bytes, options.control);
            Bytes decoded;
            if (b.method) {
                decoded.resize(b.raw);
                decode(i.methods[b.method], input, decoded, options);
            } else
                decoded = std::move(input);
            auto take = std::min<std::uint64_t>(e.size - done, b.raw - within);
            std::memcpy(result.data() + static_cast<std::size_t>(done),
                        decoded.data() + static_cast<std::size_t>(within), static_cast<std::size_t>(take));
            done += take;
            require(done == e.size || (e.offset + done) % i.block_size == 0,
                    "Chunk crosses a short decoded block");
            options.control.checkpoint("container-read", done, e.size);
        }
    } else {
        const auto &pe = i.pak_entries[index];
        auto limits = i.limits;
        limits.control = options.control;
        auto header =
            read_range(i.files[0], pe.offset, pe.header_size(), i.limits.max_index_bytes, options.control);
        Cursor h{header, limits};
        auto stored = pak_entry(h);
        if (stored.raw != pe.raw || stored.stored != pe.stored || stored.method != pe.method ||
            stored.block_size != pe.block_size || stored.flags != pe.flags || stored.blocks != pe.blocks ||
            (!pe.hash.empty() && stored.hash != pe.hash))
            throw Error("container-integrity", "Pak payload header differs from its index");
        auto payload = read_range(i.files[0], pe.offset + pe.header_size(), pe.stored,
                                  options.max_payload_bytes, options.control);
        if (sha1(payload, options.control) != stored.hash)
            throw Error("container-integrity", "Pak member stored-byte SHA-1 mismatch");
        if (!pe.method)
            result = std::move(payload);
        else {
            std::uint64_t done = 0;
            for (const auto &b : pe.blocks) {
                auto raw = std::min<std::uint64_t>(pe.block_size, pe.raw - done), size = b.second - b.first;
                if (raw > options.max_block_bytes || size > options.max_block_bytes)
                    throw Error("container-limit", "Pak block exceeds configured limit");
                auto start = static_cast<std::size_t>(b.first - pe.header_size());
                Bytes input(payload.begin() + start,
                            payload.begin() + start + static_cast<std::size_t>(size)),
                    decoded(static_cast<std::size_t>(raw));
                decode(i.methods[pe.method], input, decoded, options);
                std::memcpy(result.data() + static_cast<std::size_t>(done), decoded.data(), decoded.size());
                done += raw;
                options.control.checkpoint("container-read", done, pe.raw);
            }
        }
    }
    if (i.kind == "iostore") {
        options.control.checkpoint("container-integrity", 0, e.size);
        if (blake3_bytes(result, options.control).substr(0, 40) != e.source_hash)
            throw Error("container-integrity", "IoStore decoded chunk BLAKE3-160 mismatch");
        options.control.checkpoint("container-integrity", e.size, e.size);
    }
    options.control.checkpoint("container-read", e.size, e.size);
    return result;
}
std::vector<ContainerPair> pair_container_entries(const Container &before, const Container &after,
                                                  const OperationControl &control) {
    std::map<std::string, ContainerPair> pairs;
    auto add = [&](const Container &c, bool right) {
        for (std::size_t j = 0; j < c.entries().size(); ++j) {
            control.check();
            const auto &e = c.entries()[j];
            if (e.deleted)
                continue;
            auto key = e.path.empty() ? "id:" + e.id : "path:" + e.path;
            auto &pair = pairs[key];
            pair.key = key;
            auto &side = right ? pair.after : pair.before;
            if (side)
                throw Error("container-entry", "Duplicate container comparison key: " + key);
            side = j;
        }
    };
    add(before, false);
    add(after, true);
    std::vector<ContainerPair> result;
    result.reserve(pairs.size());
    for (auto &item : pairs) {
        control.check();
        result.push_back(std::move(item.second));
    }
    control.check();
    return result;
}
Json compare_containers(const Container &before, const Container &after,
                        const ContainerReadOptions &options) {
    auto pairs = pair_container_entries(before, after, options.control);
    std::uint64_t total = 0;
    auto budget = [&](const Container &input, const std::optional<std::size_t> &entry) {
        options.control.check();
        if (!entry)
            return;
        const auto size = input.entries()[*entry].size;
        if (size > options.max_payload_bytes || size > options.max_comparison_bytes - total)
            throw Error("container-limit", "Container comparison decoded-byte budget exceeded");
        total += size;
    };
    // Preflight all declared sizes before decoding any payload.
    for (const auto &pair : pairs) {
        budget(before, pair.before);
        budget(after, pair.after);
    }
    Json changes = Json::array();
    std::uint64_t unchanged = 0;
    for (const auto &pair : pairs) {
        options.control.check();
        const auto &key = pair.key;
        auto b = pair.before, a = pair.after;
        std::string bh, ah;
        if (b)
            bh = sha256_bytes(before.read(*b, options), options.control);
        if (a)
            ah = sha256_bytes(after.read(*a, options), options.control);
        if (b && a && bh == ah)
            ++unchanged;
        else
            changes.push_back({{"key", key},
                               {"change", !b ? "added" : (!a ? "removed" : "modified")},
                               {"beforeSha256", bh},
                               {"afterSha256", ah}});
        options.control.checkpoint("container-diff", unchanged + changes.size(), pairs.size());
    }
    return {{"schema", "cyuediff.container-diff/1"},
            {"coverage", "decoded-bytes"},
            {"status", changes.empty() ? "identical" : "changed"},
            {"unchanged", unchanged},
            {"changes", changes}};
}
} // namespace cyuediff
