#include "binary_delta_internal.hpp"
#include "archive.hpp"
#include <algorithm>
#include <limits>
#include <set>
namespace cyuediff {
namespace {
void require(bool good, const char *message) {
    if (!good)
        throw Error("binary-delta", message);
}
std::uint64_t number(const Json &j) {
    require(j.is_number_integer() && j >= 0, "Nonnegative delta integer expected");
    return j.get<std::uint64_t>();
}
void shape(const Json &j, std::initializer_list<const char *> fields) {
    require(j.is_object() && j.size() == fields.size(), "Unexpected binary delta fields");
    for (const auto *key : fields)
        require(j.contains(key), "Missing binary delta field");
}
int nibble(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    throw Error("binary-delta", "Invalid lowercase literal hex byte");
}
} // namespace
Json BinaryDelta::to_json(const BinaryDeltaOptions &o) const {
    detail::binary_delta_validate(*this, o);
    std::uint64_t charged = 0;
    auto charge = [&](std::uint64_t n) {
        require(n <= o.max_manifest_bytes - charged, "Manifest byte budget exceeded");
        charged += n;
    };
    charge(512);
    Json ops = Json::array();
    for (const auto &op : operations) {
        o.control.check();
        charge(128);
        if (op.kind == BinaryDeltaKind::copy) {
            ops.push_back({{"kind", "copy"}, {"offset", op.offset}, {"bytes", op.bytes}});
        } else {
            require(op.bytes <= std::numeric_limits<std::size_t>::max() / 2, "Literal hex size overflow");
            charge(op.bytes);
            charge(op.bytes);
            std::string hex;
            hex.reserve(op.data.size() * 2);
            constexpr char digits[] = "0123456789abcdef";
            for (std::size_t i = 0; i < op.data.size(); ++i) {
                if (!(i & 65535))
                    o.control.check();
                hex.push_back(digits[op.data[i] >> 4]);
                hex.push_back(digits[op.data[i] & 15]);
            }
            ops.push_back({{"kind", "literal"}, {"hex", std::move(hex)}});
        }
    }
    return {{"schema", "cyuediff.binary-delta/1"},
            {"blockSize", block_size},
            {"base", {{"bytes", base_bytes}, {"sha256", base_sha256}}},
            {"target", {{"bytes", target_bytes}, {"sha256", target_sha256}}},
            {"operations", std::move(ops)}};
}
BinaryDelta BinaryDelta::from_json(const Json &j, const BinaryDeltaOptions &o) {
    detail::binary_delta_options(o);
    shape(j, {"schema", "blockSize", "base", "target", "operations"});
    require(j.at("schema") == "cyuediff.binary-delta/1", "Unsupported binary delta schema");
    shape(j.at("base"), {"bytes", "sha256"});
    shape(j.at("target"), {"bytes", "sha256"});
    const auto block = number(j.at("blockSize"));
    require(block >= 16 && block <= 1024 * 1024, "Invalid manifest block size");
    BinaryDelta d;
    d.block_size = static_cast<std::uint32_t>(block);
    d.base_bytes = number(j.at("base").at("bytes"));
    d.target_bytes = number(j.at("target").at("bytes"));
    require(d.base_bytes <= o.max_input_bytes && d.target_bytes <= o.max_input_bytes,
            "Input byte budget exceeded");
    require(j.at("base").at("sha256").is_string() && j.at("target").at("sha256").is_string(),
            "SHA-256 string expected");
    for (const char *side : {"base", "target"}) {
        const auto &digest = j.at(side).at("sha256").get_ref<const std::string &>();
        require(digest.size() == 64 && digest.find_first_not_of("0123456789abcdef") == std::string::npos,
                "Invalid SHA-256 identity");
    }
    d.base_sha256 = j.at("base").at("sha256").get<std::string>();
    d.target_sha256 = j.at("target").at("sha256").get<std::string>();
    const auto &ops = j.at("operations");
    require(ops.is_array() && ops.size() <= o.max_operations, "Operation budget exceeded");
    std::uint64_t literals = 0, charged = 512;
    require(charged <= o.max_manifest_bytes, "Manifest byte budget exceeded");
    for (const auto &op : ops) {
        o.control.check();
        require(op.is_object() && op.contains("kind"), "Invalid delta operation");
        require(128 <= o.max_manifest_bytes - charged, "Manifest byte budget exceeded");
        charged += 128;
        if (op.at("kind") == "copy") {
            shape(op, {"kind", "offset", "bytes"});
            d.operations.push_back(
                {BinaryDeltaKind::copy, number(op.at("offset")), number(op.at("bytes")), {}});
        } else {
            require(op.at("kind") == "literal", "Unknown delta operation kind");
            shape(op, {"kind", "hex"});
            require(op.at("hex").is_string(), "Literal hex string expected");
            const auto &hex = op.at("hex").get_ref<const std::string &>();
            require(!hex.empty() && !(hex.size() & 1), "Invalid literal hex length");
            require(hex.size() <= o.max_manifest_bytes - charged, "Manifest byte budget exceeded");
            charged += hex.size();
            require(hex.size() / 2 <= o.max_literal_bytes - literals, "Literal byte budget exceeded");
            literals += hex.size() / 2;
            BinaryDeltaOperation literal{BinaryDeltaKind::literal, 0, hex.size() / 2, {}};
            literal.data.reserve(hex.size() / 2);
            for (std::size_t i = 0; i < hex.size(); i += 2) {
                if (!(i & 65535))
                    o.control.check();
                literal.data.push_back(std::uint8_t((nibble(hex[i]) << 4) | nibble(hex[i + 1])));
            }
            d.operations.push_back(std::move(literal));
        }
    }
    d.summary(o);
    return d;
}
BinaryDelta BinaryDelta::parse(const std::string &bytes, const BinaryDeltaOptions &o) {
    detail::binary_delta_options(o);
    require(bytes.size() <= o.max_manifest_bytes, "Manifest byte budget exceeded");
    std::vector<std::set<std::string>> keys;
    std::size_t objects = 0;
    auto j = Json::parse(bytes, [&](int depth, Json::parse_event_t event, Json &v) {
        o.control.check();
        require(depth <= 8, "Manifest nesting limit exceeded");
        if (event == Json::parse_event_t::object_start) {
            require(objects <= o.max_operations || objects - o.max_operations < 3,
                    "Manifest object budget exceeded");
            ++objects;
            keys.emplace_back();
        } else if (event == Json::parse_event_t::key) {
            require(!keys.empty() && keys.back().insert(v.get<std::string>()).second,
                    "Duplicate manifest field");
        } else if (event == Json::parse_event_t::object_end)
            keys.pop_back();
        return true;
    });
    return from_json(j, o);
}
std::vector<std::uint8_t> apply_binary_delta(const std::vector<std::uint8_t> &base, const BinaryDelta &d,
                                             const BinaryDeltaOptions &o) {
    detail::binary_delta_validate(d, o);
    require(base.size() == d.base_bytes && sha256_bytes(base, o.control) == d.base_sha256,
            "Base size or SHA-256 does not match delta identity");
    std::vector<std::uint8_t> result;
    require(d.target_bytes <= result.max_size(), "Target allocation exceeds address space");
    result.reserve(static_cast<std::size_t>(d.target_bytes));
    for (const auto &op : d.operations) {
        const auto &input = op.kind == BinaryDeltaKind::copy ? base : op.data;
        const auto start = op.kind == BinaryDeltaKind::copy ? op.offset : 0;
        for (std::uint64_t at = 0; at < op.bytes;) {
            o.control.checkpoint("Reconstructing binary target", result.size(), d.target_bytes);
            const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(op.bytes - at, 1024 * 1024));
            result.insert(result.end(), input.begin() + static_cast<std::size_t>(start + at),
                          input.begin() + static_cast<std::size_t>(start + at) + n);
            at += n;
        }
    }
    require(sha256_bytes(result, o.control) == d.target_sha256,
            "Reconstructed target SHA-256 does not match delta identity");
    o.control.checkpoint("Reconstructing binary target", result.size(), d.target_bytes);
    return result;
}
std::vector<std::uint8_t> read_binary_file(const fs::path &path, std::uint64_t max_bytes,
                                           const OperationControl &control) {
    require(max_bytes != 0, "File read budget must be nonzero");
    ReadOptions read;
    read.control = control;
    detail::Archive a(path, read);
    std::vector<std::uint8_t> result;
    require(a.size() <= max_bytes && a.size() <= result.max_size(), "Binary file byte budget exceeded");
    result.resize(static_cast<std::size_t>(a.size()));
    for (std::size_t at = 0; at < result.size();) {
        control.checkpoint("Reading binary file", at, result.size());
        const auto n = std::min<std::size_t>(result.size() - at, 1024 * 1024);
        a.read(result.data() + at, n);
        at += n;
    }
    require(fs::file_size(path) == result.size(), "Binary file size changed while reading");
    control.checkpoint("Reading binary file", result.size(), result.size());
    return result;
}
BinaryDelta make_binary_delta_files(const fs::path &base, const fs::path &target,
                                    const BinaryDeltaOptions &o) {
    detail::binary_delta_options(o);
    auto a = read_binary_file(base, o.max_input_bytes, o.control);
    auto b = read_binary_file(target, o.max_input_bytes, o.control);
    return make_binary_delta(a, b, o);
}
BinaryDelta read_binary_delta(const fs::path &path, const BinaryDeltaOptions &o) {
    auto bytes = read_binary_file(path, o.max_manifest_bytes, o.control);
    return BinaryDelta::parse(std::string(bytes.begin(), bytes.end()), o);
}
} // namespace cyuediff
