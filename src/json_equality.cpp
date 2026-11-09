#include "json_equality.hpp"
#include <cmath>
namespace cyuediff::detail {
namespace {
bool float_integer(double value, const Json &integer) {
    if (!std::isfinite(value) || std::trunc(value) != value)
        return false;
    if (integer.is_number_unsigned())
        return value >= 0.0 && value < std::ldexp(1.0, 64) &&
               static_cast<std::uint64_t>(value) == integer.get<std::uint64_t>();
    return value >= -std::ldexp(1.0, 63) && value < std::ldexp(1.0, 63) &&
           static_cast<std::int64_t>(value) == integer.get<std::int64_t>();
}
struct Equality {
    const OperationControl &control;
    std::size_t nodes = 0;
    bool same(const Json &a, const Json &b, unsigned depth = 0) {
        if (depth > 256)
            throw Error("comparison-depth", "JSON comparison depth exceeds its budget");
        if (!(++nodes & 1023))
            control.check();
        if (a.is_number() && b.is_number())
            return numeric_equal(a, b);
        if (a.type() != b.type())
            return false;
        if (a.is_object()) {
            if (a.size() != b.size())
                return false;
            auto i = a.begin(), j = b.begin();
            for (; i != a.end(); ++i, ++j)
                if (i.key() != j.key() || !same(*i, *j, depth + 1))
                    return false;
            return true;
        }
        if (a.is_array()) {
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i < a.size(); ++i)
                if (!same(a[i], b[i], depth + 1))
                    return false;
            return true;
        }
        return a == b;
    }
};
} // namespace
bool numeric_equal(const Json &a, const Json &b) {
    if (!a.is_number() || !b.is_number())
        return false;
    if (a.is_number_float())
        return b.is_number_float() ? a.get<double>() == b.get<double>() : float_integer(a.get<double>(), b);
    if (b.is_number_float())
        return float_integer(b.get<double>(), a);
    if (a.is_number_unsigned()) {
        if (b.is_number_unsigned())
            return a.get<std::uint64_t>() == b.get<std::uint64_t>();
        const auto value = b.get<std::int64_t>();
        return value >= 0 && a.get<std::uint64_t>() == static_cast<std::uint64_t>(value);
    }
    if (b.is_number_unsigned()) {
        const auto value = a.get<std::int64_t>();
        return value >= 0 && static_cast<std::uint64_t>(value) == b.get<std::uint64_t>();
    }
    return a.get<std::int64_t>() == b.get<std::int64_t>();
}
bool json_equal(const Json &a, const Json &b, const OperationControl &control) {
    control.check();
    Equality comparison{control};
    return comparison.same(a, b);
}
} // namespace cyuediff::detail
