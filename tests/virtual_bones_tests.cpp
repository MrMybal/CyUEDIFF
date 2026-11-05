#include "attachments_model.hpp"
#include <cyuediff/attachments.hpp>
#include <iostream>
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
        auto p = virtual_bones_model();
        AttachmentOptions options;
        options.source_indices = false;
        const auto snapshot = attachment_snapshot(p, options);
        const auto &v = snapshot["owners"]["Rig"]["virtualBones"];
        check(snapshot["complete"] && v["definitions"].size() == 2, "Stored virtual definitions");
        check(v["definitions"]["name:VB_tip"]["sourceResolution"] == "virtual" &&
                  v["definitions"]["name:VB_tip"]["targetResolution"] == "raw",
              "Forward-independent named dependency resolution");
        auto order = p;
        std::swap(order.exports[0].properties["VirtualBones"]["value"][0],
                  order.exports[0].properties["VirtualBones"]["value"][1]);
        check(compare_attachments(p, order).status() == "identical",
              "Unique definition reorder does not change named projection");
        auto changed = p;
        changed.exports[0].properties["VirtualBones"]["value"][0]["TargetBoneName"]["value"] = "tip";
        const auto diff = compare_attachments(p, changed);
        check(diff.status() == "changed" && diff.changes.size() == 1 &&
                  diff.changes[0]["path"] == "/owners/Rig/virtualBones/definitions/name:VB~0~1Grip/target",
              "Escaped virtual-bone identity difference");
        auto cycle = p;
        cycle.exports[0].properties["VirtualBones"]["value"][0]["SourceBoneName"]["value"] = "VB_tip";
        const auto cyclic = attachment_snapshot(cycle);
        check(!cyclic["complete"].get<bool>() &&
                  cyclic["owners"]["Rig"]["virtualBones"]["definitions"]["name:VB_tip"]["cycleBlocked"],
              "Cycle blocks dependent definitions without recursive rebuilding");
        auto missing = p;
        missing.exports[0].properties["VirtualBones"]["value"][0]["SourceBoneName"]["value"] = "missing";
        check(!attachment_snapshot(missing)["complete"].get<bool>(),
              "Unresolved source retained without invented bone");
        auto duplicate = p;
        duplicate.exports[0].properties["VirtualBones"]["value"][1]["VirtualBoneName"]["value"] = "VB~/Grip";
        const auto ambiguous = attachment_snapshot(duplicate);
        check(!ambiguous["complete"].get<bool>() &&
                  ambiguous["owners"]["Rig"]["virtualBones"]["definitions"].contains("index:1"),
              "Duplicate virtual names use explicit index identities");
        auto collide = p;
        collide.exports[0].properties["VirtualBones"]["value"][0]["VirtualBoneName"]["value"] = "arm";
        check(!attachment_snapshot(collide)["complete"].get<bool>(),
              "Raw/virtual name collision does not infer correspondence");
        auto omitted = p;
        omitted.exports[0].properties.erase("VirtualBones");
        const auto absent = attachment_snapshot(omitted);
        check(!absent["complete"].get<bool>() && absent["owners"]["Rig"]["socketsComplete"],
              "Missing virtual metadata independent of stored socket-list completeness");
        auto malformed = p;
        malformed.exports[0].properties["VirtualBones"]["value"][0].erase("VirtualBoneName");
        check(!attachment_snapshot(malformed)["complete"].get<bool>(),
              "Missing definition names retained as unresolved indexed entries");
        auto unknown = p;
        unknown.exports[0].native_coverage = "opaque";
        check(!attachment_snapshot(unknown)["complete"].get<bool>(), "No raw name-table guessing");
        auto empty = attachments_model();
        empty.exports[0].native_coverage = "opaque";
        check(attachment_snapshot(empty)["complete"], "Known empty virtual table needs no unused hierarchy");
        for (int kind = 0; kind < 3; ++kind) {
            AttachmentOptions budget;
            if (kind == 0)
                budget.max_virtual_bones = 1;
            if (kind == 1)
                budget.max_references = 5;
            if (kind == 2)
                budget.max_reference_bones = 2;
            bool rejected = false;
            try {
                attachment_snapshot(p, budget);
            } catch (const Error &e) {
                rejected = e.code == "limit";
            }
            check(rejected, "Shared virtual/reference/raw-name budgets");
        }
        bool stop = false;
        AttachmentOptions cancel;
        cancel.control.cancelled = [&] { return stop; };
        cancel.control.progress = [&](const OperationProgress &progress) {
            if (progress.phase == "Projecting virtual-bone definitions")
                stop = true;
        };
        bool rejected = false;
        try {
            attachment_snapshot(p, cancel);
        } catch (const Error &e) {
            rejected = e.code == "cancelled";
        }
        check(rejected, "Virtual metadata cancellation propagates");
        auto many = attachments_model();
        auto &entries = many.exports[0].properties["VirtualBones"]["value"];
        for (int i = 0; i < 2000; ++i)
            entries.push_back(
                virtual_definition("V" + std::to_string(i), i ? "V" + std::to_string(i - 1) : "root", "arm"));
        check(attachment_snapshot(many)["complete"], "Long dependency chain handled iteratively");
        std::cout << checks << " virtual-bone checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
