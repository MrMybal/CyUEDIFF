#include "../src/json_equality.hpp"
#include <cyuediff/property_page.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
using namespace cyuediff;
namespace {
unsigned checks = 0;
void check(bool v, const char *why) {
    ++checks;
    if (!v)
        throw std::runtime_error(why);
}
} // namespace
int main() {
    try {
        auto test = [](Json a, Json b, bool equal) {
            check(detail::numeric_equal(a, b) == equal && detail::numeric_equal(b, a) == equal,
                  "Symmetric exact numeric equality");
            check(detail::json_equal(Json{{"nested", Json::array({a})}},
                                     Json{{"nested", Json::array({b})}}) == equal,
                  "Exact equality inside borrowed structured values");
        };
        test(std::int64_t(9007199254740993LL), 9007199254740992.0, false);
        test(std::int64_t(9007199254740994LL), 9007199254740994.0, true);
        test(std::uint64_t(9007199254740993ULL), 9007199254740992.0, false);
        test(std::numeric_limits<std::int64_t>::max(), std::ldexp(1.0, 63), false);
        test(std::numeric_limits<std::int64_t>::min(), -std::ldexp(1.0, 63), true);
        test(std::numeric_limits<std::uint64_t>::max(), std::ldexp(1.0, 64), false);
        test(std::uint64_t(18446744073709549568ULL), std::nextafter(std::ldexp(1.0, 64), 0.0), true);
        test(std::int64_t(-1), std::numeric_limits<std::uint64_t>::max(), false);
        test(std::int64_t(42), std::uint64_t(42), true);
        test(std::int64_t(-42), -42.0, true);
        test(0, -0.0, true);
        test(0, .25, false);
        test(0, std::numeric_limits<double>::denorm_min(), false);
        test(1, std::numeric_limits<double>::infinity(), false);
        Json a = std::int64_t(9007199254740993LL), b = 9007199254740992.0;
        check(classify_change(&a, &b) == ChangeKind::modified,
              "Graph/property classification does not hide a rounded integer");
        auto table = property_table(&a, &b);
        check(table.rows[0].change == ChangeKind::modified && table.rows[0].before == "9007199254740993",
              "Expanded values retain exact large integer differences");
        auto page = property_page(&a, &b, "");
        check(page.table.rows[0].change == ChangeKind::modified,
              "Paged values retain exact large integer differences");
        {
            // Inspector rows follow the report's array alignment, in the longer array's indices.
            const Json before = {1, 2, 3, 4}, inserted = {0, 1, 2, 3, 4}, removed = {1, 3, 4};
            auto rows = property_table(&before, &inserted).rows;
            check(rows.size() == 6 && rows[1].path == "/0" && rows[1].change == ChangeKind::added &&
                      std::all_of(rows.begin() + 2, rows.end(),
                                  [](const PropertyRow &r) { return r.change == ChangeKind::unchanged; }) &&
                      rows[2].before == "1" && rows[5].after == "4",
                  "An inserted element is one added row; later rows stay unchanged");
            rows = property_table(&before, &removed).rows;
            check(rows.size() == 5 && rows[2].path == "/1" && rows[2].change == ChangeKind::removed &&
                      rows[3].change == ChangeKind::unchanged && rows[3].before == "3" && rows[3].after == "3",
                  "A removed element is one removed row in before indices");
            const auto page = property_page(&before, &inserted, "");
            check(page.total_children == 5 && page.table.rows[1].change == ChangeKind::added &&
                      page.table.rows[4].change == ChangeKind::unchanged,
                  "Paged children use the same alignment");
            PropertyTableOptions paired;
            paired.coverage.comparison_unavailable = {"/3"};
            rows = property_table(&before, &inserted, paired).rows;
            check(rows[1].change == ChangeKind::modified && rows[1].before == "1" && rows[1].after == "0",
                  "Arrays with unavailable scopes keep index correspondence");
        }
        Json array = Json::array();
        for (int i = 0; i < 4096; ++i)
            array.push_back(1);
        unsigned calls = 0;
        OperationControl control;
        control.cancelled = [&] { return ++calls > 1; };
        try {
            detail::json_equal(array, array, control);
            throw std::runtime_error("Expected equality cancellation");
        } catch (const Error &e) {
            check(e.code == "cancelled", "Large equal subtrees honor cancellation");
        }
        check(!detail::json_equal(Json("1"), Json(1)) && !detail::json_equal(Json(nullptr), Json::object()),
              "Non-numeric JSON types remain distinct");
        std::cout << checks << " exact JSON equality checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << checks << ": " << e.what() << '\n';
        return 1;
    }
}
