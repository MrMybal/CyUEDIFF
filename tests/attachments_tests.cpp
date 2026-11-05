#include "attachments_model.hpp"
#include <cyuediff/attachments.hpp>
#include <iostream>
#include <limits>
using namespace cyuediff;
using namespace cyuediff::test;
int main() {
    int checks = 0;
    try {
        auto check = [&](bool v, const char *msg) {
            ++checks;
            if (!v)
                throw std::runtime_error(msg);
        };
        auto p = attachments_model();
        const auto original = p.to_json();
        const auto a = attachment_snapshot(p);
        check(a["complete"] && a["sockets"].size() == 2, "Two reflected socket objects");
        const auto &grip = a["sockets"]["Rig.Grip"];
        check(grip["name"] == "Grip" && grip["bone"] == "arm" && grip["outer"] == "Rig",
              "Stored exact identity and outer");
        check(grip["location"] == Json({1., 2., 3.}) && grip["rotation"] == Json({0., 90., 0.}) &&
                  grip["forceAlwaysAnimated"],
              "Stored socket parameters");
        check(a["owners"]["Rig"]["socketReferences"][0]["resolved"] &&
                  a["owners"]["Rig"]["containedSockets"].size() == 2,
              "Explicit membership separate from containment");
        auto changed = p;
        changed.exports[1].properties["RelativeLocation"]["value"][0] = 4.;
        const auto diff = compare_attachments(p, changed);
        check(diff.status() == "changed" && diff.changes.size() == 1 &&
                  diff.changes[0]["path"] == "/sockets/Rig.Grip/location/0",
              "Exact socket path difference");
        auto order = p;
        std::swap(order.exports[1], order.exports[2]);
        check(compare_attachments(p, order).status() == "identical",
              "Export ordering does not change object-path identity");
        std::swap(order.exports[0].properties["Sockets"]["value"][0],
                  order.exports[0].properties["Sockets"]["value"][1]);
        check(compare_attachments(p, order).status() == "changed",
              "Stored membership order retained for lookup precedence");
        auto missing = p;
        missing.exports[1].properties.erase("RelativeLocation");
        const auto m = attachment_snapshot(missing);
        check(m["complete"] && !m["sockets"]["Rig.Grip"].contains("location"),
              "Omitted local values stay absent; metadata does not synthesize defaults");
        missing.exports[0].properties.erase("Sockets");
        check(!attachment_snapshot(missing)["complete"].get<bool>(), "Omitted owner list stays unknown");
        auto external = p;
        external.exports[0].properties["Sockets"]["value"].push_back("/Other.Socket");
        check(!attachment_snapshot(external)["owners"]["Rig"]["socketsComplete"].get<bool>(),
              "External socket not silently loaded");
        auto orphan = p;
        orphan.exports[0].properties["Sockets"]["value"] = Json::array();
        const auto o = attachment_snapshot(orphan);
        check(o["owners"]["Rig"]["socketReferences"].empty() &&
                  o["owners"]["Rig"]["containedSockets"].size() == 2,
              "Unlisted child never inferred as membership");
        auto duplicate = p;
        duplicate.exports[2].properties["SocketName"]["value"] = "Grip";
        check(attachment_snapshot(duplicate)["sockets"].size() == 2 &&
                  !attachment_snapshot(duplicate)["owners"]["Rig"]["diagnostics"].empty(),
              "Duplicate labels preserve object identities and precedence diagnostics");
        auto bad = p;
        bad.exports[1].properties["RelativeLocation"]["value"] = {1., 2.};
        check(!attachment_snapshot(bad)["complete"].get<bool>(),
              "Malformed stored geometry stays unresolved");
        bad = p;
        bad.exports[0].properties["Sockets"]["type"]["parameters"][0]["name"] = "NameProperty";
        check(attachment_snapshot(bad)["owners"]["Rig"]["socketListCoverage"] == "malformed",
              "Wrong reference tag is not interpreted as sockets");
        bad = p;
        bad.exports[1].outer_index = 999;
        check(!attachment_snapshot(bad)["sockets"]["Rig.Grip"]["complete"].get<bool>(),
              "Invalid outer retained as unresolved");
        for (int kind = 0; kind < 5; ++kind) {
            AttachmentOptions options;
            if (kind == 0)
                options.max_sockets = 1;
            if (kind == 1)
                options.max_references = 1;
            if (kind == 2)
                options.max_owners = 0;
            if (kind == 3)
                options.max_nodes = 1;
            if (kind == 4)
                options.max_text_bytes = 1;
            bool rejected = false;
            try {
                attachment_snapshot(p, options);
            } catch (const Error &e) {
                rejected = e.code == "limit";
            }
            check(rejected, "Socket aggregate budgets enforced");
        }
        AttachmentOptions options;
        options.control.cancelled = [] { return true; };
        bool rejected = false;
        try {
            attachment_snapshot(p, options);
        } catch (const Error &e) {
            rejected = e.code == "cancelled";
        }
        check(rejected, "Socket cancellation propagates");
        bad = p;
        bad.exports[1].properties["RelativeLocation"]["value"][0] = std::numeric_limits<double>::infinity();
        rejected = false;
        try {
            attachment_snapshot(bad);
        } catch (const Error &e) {
            rejected = e.code == "attachment-value";
        }
        check(rejected, "Nonfinite host properties are rejected");
        bad = p;
        bad.exports.push_back(bad.exports[1]);
        rejected = false;
        try {
            attachment_snapshot(bad);
        } catch (const Error &e) {
            rejected = e.code == "attachment-identity";
        }
        check(rejected, "Duplicate paths rejected");
        p.complete_input = false;
        check(compare_attachments(p, p).status() == "incomplete",
              "Missing source input cannot establish equality");
        p.complete_input = true;
        check(original == p.to_json(), "Read-only attachment projection");
        std::cout << checks << " socket attachment checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
