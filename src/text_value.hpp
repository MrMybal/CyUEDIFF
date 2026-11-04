#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff::detail {
enum class TextScalar { uint8, int8, int32, uint32, int64, uint64, float32, float64, string, name };
// Both package backends supply bounded primitive reads and their enclosing budgets.
struct TextWireReader {
    std::function<Json(TextScalar, unsigned)> scalar;
    std::function<void(unsigned)> check;
    std::function<std::uint64_t()> position;
    std::function<std::uint32_t()> remaining;
    bool invariant_marker = true;
    const char *type_error = "text-history", *value_error = "text-value", *limit_error = "limit",
               *identity_error = "text-identity";
    std::uint32_t count(unsigned depth) {
        const auto n = scalar(TextScalar::int32, depth + 1).get<std::int32_t>();
        if (n < 0)
            throw Error(value_error, "Negative text argument count", position());
        if (static_cast<std::uint32_t>(n) > remaining())
            throw Error(limit_error, "Text arguments exceed remaining work", position());
        return static_cast<std::uint32_t>(n);
    }
    Json argument(unsigned depth) {
        check(depth);
        const int id = scalar(TextScalar::int8, depth + 1).get<int>();
        static const char *tokens[] = {"int64", "uint64", "float32", "float64", "text", "gender"};
        if (id < 0 || id > 5)
            throw Error(type_error, "Unsupported text argument type", position());
        Json value;
        switch (id) {
        case 0:
            value = scalar(TextScalar::int64, depth + 1);
            break;
        case 1:
        case 5:
            value = scalar(TextScalar::uint64, depth + 1);
            break;
        case 2:
            value = scalar(TextScalar::float32, depth + 1);
            break;
        case 3:
            value = scalar(TextScalar::float64, depth + 1);
            break;
        case 4:
            value = text(depth + 1);
            break;
        }
        return {{"argumentType", tokens[id]}, {"value", std::move(value)}};
    }
    Json text(unsigned depth, bool account = true) {
        if (account)
            check(depth);
        auto read = [&](TextScalar kind) { return scalar(kind, depth + 1); };
        Json value = {{"flags", read(TextScalar::uint32)}, {"history", read(TextScalar::int8)}};
        const auto history = value["history"].get<int>();
        if (history == -1) {
            const auto present = invariant_marker ? read(TextScalar::uint32).get<std::uint32_t>() : 0u;
            if (present > 1)
                throw Error(value_error, "Invalid culture-invariant text marker", position());
            value["kind"] = present ? "culture-invariant" : "none";
            value["source"] = present ? read(TextScalar::string) : Json("");
        } else if (history == 0) {
            value["kind"] = "localized";
            value["namespace"] = read(TextScalar::string);
            value["key"] = read(TextScalar::string);
            value["source"] = read(TextScalar::string);
        } else if (history == 1 || history == 2) {
            const bool named = history == 1;
            value["kind"] = named ? "named-format" : "ordered-format";
            value["format"] = text(depth + 1);
            const auto size = count(depth);
            Json arguments = named ? Json::object() : Json::array();
            for (std::uint32_t i = 0; i < size; ++i) {
                if (named) {
                    auto name = scalar(TextScalar::string, depth + 2).get<std::string>();
                    if (arguments.contains(name))
                        throw Error(identity_error, "Duplicate named text argument", position());
                    arguments[std::move(name)] = argument(depth + 2);
                } else {
                    arguments.push_back(argument(depth + 2));
                }
            }
            value["arguments"] = std::move(arguments);
        } else if (history == 11) {
            value["kind"] = "string-table";
            value["table"] = read(TextScalar::name);
            value["key"] = read(TextScalar::string);
        } else if (history == 10) {
            value["kind"] = "transform";
            value["source"] = text(depth + 1);
            auto operation = read(TextScalar::uint8).get<unsigned>();
            if (operation > 1)
                throw Error(value_error, "Unsupported text transform operation", position());
            value["transformType"] = operation;
        } else {
            throw Error(type_error, "Unsupported text history: " + std::to_string(history), position());
        }
        return value;
    }
};
} // namespace cyuediff::detail
