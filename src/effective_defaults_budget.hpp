#pragma once
#include <cyuediff/defaults.hpp>
#include <algorithm>
#include <cmath>
namespace cyuediff::detail {
inline std::string effective_token(const std::string &text) {
    std::string out;
    for (char c : text)
        out += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
    return out;
}
struct EffectiveBudget {
    const EffectiveDefaultsOptions &options;
    explicit EffectiveBudget(const EffectiveDefaultsOptions &value) : options(value) {}
    std::uint32_t nodes = 0;
    std::uint64_t text = 0;
    void spend(std::uint32_t n = 1, std::uint64_t b = 0) {
        options.control.check();
        if (n > options.max_nodes - nodes || b > options.max_text_bytes - text)
            throw Error("default-limit", "Effective default work/text budget exceeded");
        nodes += n;
        text += b;
        if (options.charge)
            options.charge(n, b);
    }
    void depth(unsigned d) {
        spend();
        if (d >= options.max_depth)
            throw Error("default-limit", "Effective default depth budget exceeded");
    }
    Json copy(const Json &value, unsigned d = 0) {
        depth(d);
        if (value.is_string())
            spend(0, value.get_ref<const std::string &>().size());
        if (value.is_object()) {
            Json out = Json::object();
            for (auto it = value.begin(); it != value.end(); ++it) {
                spend(0, it.key().size());
                out[it.key()] = copy(it.value(), d + 1);
            }
            return out;
        }
        if (value.is_array()) {
            Json out = Json::array();
            for (const auto &item : value)
                out.push_back(copy(item, d + 1));
            return out;
        }
        if (value.is_binary() || value.is_discarded() ||
            (value.is_number_float() && !std::isfinite(value.get<double>())))
            throw Error("default-unavailable", "Unsupported effective value");
        return value;
    }
    std::string path_child(const std::string &parent, const std::string &part) {
        std::uint64_t bytes = 1;
        std::size_t i = 0;
        if (bytes > options.max_text_bytes)
            throw Error("default-limit", "Effective path exceeds text budget");
        for (char c : part) {
            if (!(i++ & 4095))
                options.control.check();
            const auto n = c == '/' || c == '~' ? 2u : 1u;
            if (n > options.max_text_bytes - bytes)
                throw Error("default-limit", "Effective path exceeds text budget");
            bytes += n;
        }
        if (parent.size() > options.max_text_bytes || bytes > options.max_text_bytes - parent.size())
            throw Error("default-limit", "Effective path exceeds text budget");
        spend(1, parent.size() + bytes);
        return parent + "/" + effective_token(part);
    }

    void inspect(const Json &value, unsigned d = 0) {
        depth(d);
        if (value.is_string())
            spend(0, value.get_ref<const std::string &>().size());
        else if (value.is_object())
            for (auto it = value.begin(); it != value.end(); ++it) {
                spend(0, it.key().size());
                inspect(it.value(), d + 1);
            }
        else if (value.is_array())
            for (const auto &v : value)
                inspect(v, d + 1);
        else if (value.is_binary() || value.is_discarded() ||
                 (value.is_number_float() && !std::isfinite(value.get<double>())))
            throw Error("default-unavailable", "Unsupported effective value");
    }
};
} // namespace cyuediff::detail
