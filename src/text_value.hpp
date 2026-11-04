#pragma once
#include <cyuediff/core.hpp>
namespace cyuediff::detail {
enum class TextScalar { uint8, int8, int32, uint32, int64, uint64, float32, float64, string, name };
// Both package backends supply bounded primitive reads and their enclosing budgets.
struct TextWireReader {
    std::function<Json(TextScalar, unsigned)> scalar;
    std::function<void(unsigned)> check;
    std::function<std::uint64_t()> position;
    bool invariant_marker = true;
    const char *type_error = "text-history", *value_error = "text-value";
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
