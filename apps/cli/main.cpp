#include "binary_commands.hpp"
#include "effective_commands.hpp"
#include "../../src/json_output.hpp"
#include "../../src/atomic_output.hpp"
#include <cyuediff/curves.hpp>
#include <cyuediff/animation.hpp>
#include <cyuediff/cooked_animation.hpp>
#include <cyuediff/animation_pose.hpp>
#include <cyuediff/animation_socket_pose.hpp>
#include <cyuediff/core.hpp>
#include <cyuediff/collision.hpp>
#include <cyuediff/collision_wireframe.hpp>
#include <cyuediff/skeleton_pose.hpp>
#include <cyuediff/attachments_pose.hpp>
#include <cyuediff/catalog.hpp>
#include <cyuediff/hdr.hpp>
#include <cyuediff/container.hpp>
#include <cyuediff/zen.hpp>
#include <cyuediff/cooked_texture.hpp>
#include <cyuediff/cooked_mesh.hpp>
#include <cyuediff/cooked_skeletal.hpp>
#include <cyuediff/physical_cloth.hpp>
#include <cyuediff/cooked_physics.hpp>
#include <cyuediff/schema_context.hpp>
#include <cyuediff/binary_layout.hpp>
#include <cyuediff/tagged_types.hpp>
#include <cyuediff/source_limits.hpp>
#include <cmath>
#include <locale>
#include <sstream>
#ifdef CYUEDIFF_HAS_GIT
#include <cyuediff/git.hpp>
#endif
#include <fstream>
#include <iostream>
#include <set>
#include <limits>
using namespace cyuediff;
namespace {
int run(const std::vector<std::string> &args) {
    if (args.size() == 1 && args[0] == "--version") {
        std::cout << CYUEDIFF_VERSION << '\n';
        return 0;
    }
    if (args.empty() || args[0] == "--help" || args[0] == "help") {
        std::cout
            << "CyUEDiff " CYUEDIFF_VERSION " - standalone Unreal package reader\n"
               "  cyuediff binary-diff BASE TARGET [--block-size BYTES] [--summary] [--out DELTA.json]\n"
               "  cyuediff binary-apply BASE DELTA --out NEW_FILE\n"
               "  cyuediff binary-compose FIRST_DELTA SECOND_DELTA [--format json|binary] [--summary] [--out "
               "NEW_DELTA]\n"
               "  cyuediff binary-info DELTA [--out NEW_REPORT.json]\n"
               "  binary-diff and git-binary-diff accept --format json|binary (binary requires --out).\n"
               "  binary-compose accepts --max-composition-fragments COUNT.\n"
               "  binary-diff and git-binary-diff accept --max-source-read-bytes BYTES.\n"
               "  cyuediff git-binary-diff REPO BEFORE_REV AFTER_REV PATH [--after-path PATH] [--summary] "
               "[--out DELTA.json]\n"
               "  Binary commands refuse existing outputs; see docs/BINARY_DELTA.md for budgets.\n"
               "  cyuediff source-types MAPPING.usmap UE4_VERSION UE5_VERSION ROOT [ROOT...]\n"
               "    [--layouts BASE.json] [--codec MODULE.dll] [--out CATALOGUE.json]\n"
               "  cyuediff layouts DESCRIPTION.json [--out REPORT.json]\n"
               "  cyuediff effective-inspect ASSET CONTEXT.json [--object EXPORT] [--expect-context SHA256] "
               "[--out NEW_REPORT.json]\n"
               "  cyuediff effective-diff BEFORE AFTER --before-context JSON --after-context JSON [--out "
               "NEW_REPORT.json]\n"
               "  inspect/diff/git-inspect/git-diff accept --layouts DESCRIPTION.json.\n"
               "  diff/git-diff accept independent --after-layouts DESCRIPTION.json.\n"
               "  inspect/git-inspect/zen-inspect accept --name-identities for typed FName components.\n"
               "  Source commands accept --max-properties COUNT and --max-property-bytes BYTES.\n"
               "  Comparisons accept independent --after-max-properties and --after-max-property-bytes.\n"
               "  cyuediff inspect ASSET [--metadata] [--snapshot] [--project PROJECT] [--engine-root "
               "ROOT] [--out FILE]\n"
               "  cyuediff diff BEFORE AFTER [--project PROJECT] [--before-project PROJECT] "
               "[--after-project PROJECT] [--engine-root ROOT] [--out FILE]\n"
               "  cyuediff graph ASSET [--project PROJECT] [--out FILE]\n"
               "  cyuediff texture ASSET [--project PROJECT] [--out FILE]\n"
               "  cyuediff preview ASSET --out IMAGE.png [--mip N] [--object NAME]\n"
               "  cyuediff image-diff BEFORE AFTER [--threshold 0..255] [--mip N] [--object NAME] "
               "[--heatmap MASK.png] [--out REPORT.json]\n"
               "  cyuediff hdr-diff BEFORE AFTER [--threshold SAMPLE_DELTA] [--heatmap MASK.png] [--codec "
               "MODULE.dll]\n"
               "  Source decode commands accept --payload-root DIRECTORY for a local virtualized store.\n"
               "  image-diff/hdr-diff/mesh-diff accept independent --after-payload-root DIRECTORY.\n"
               "  preview accepts --exposure EV for HDR source display.\n"
               "  cyuediff curves ASSET [--project PROJECT] [--out FILE]\n"
               "  cyuediff curves-diff BEFORE AFTER [--before-object PATH] [--after-object PATH] [--out "
               "FILE]\n"
               "  cyuediff curve-value-diff BEFORE AFTER --time-start N --time-end N [--samples N]\n"
               "    [--before-object PATH] [--after-object PATH] [--before-lane KEY] [--after-lane KEY] "
               "[--value-tolerance N]\n"
               "  Curve commands accept --curve-family float|transform (AnimDataModel scope).\n"
               "  cyuediff curve-components ASSET --time N [--curve-id ID] [--curve-family float|transform] "
               "[--object PATH]\n"
               "  cyuediff curve-value ASSET --time NUMBER [--object PATH] [--lane KEY] [--out FILE]\n"
               "  cyuediff animation ASSET [--project PROJECT] [--out FILE]\n"
               "  cyuediff animation-diff BEFORE AFTER [--before-object PATH] [--after-object PATH] [--out "
               "FILE]\n"
               "  cyuediff animation-frame-diff BEFORE AFTER --before-frame N --after-frame N\n"
               "    [--before-object PATH] [--after-object PATH] [--position-tolerance N]\n"
               "    [--rotation-tolerance DEGREES] [--scale-tolerance N] [--out FILE]\n"
               "  cyuediff animation-socket-pose ANIMATION SKELETON --frame N [--object PATH] "
               "[--skeleton-object PATH]\n"
               "  cyuediff animation-socket-pose-diff BEFORE AFTER --before-skeleton FILE --after-skeleton "
               "FILE\n"
               "    --before-frame N --after-frame N [--position-tolerance N] [--out FILE]\n"
               "  cyuediff animation-pose ANIMATION SKELETON --frame N [--object PATH] [--skeleton-object "
               "PATH]\n"
               "  cyuediff animation-pose-diff BEFORE AFTER --before-skeleton FILE --after-skeleton FILE\n"
               "    --before-frame N --after-frame N [--before-object PATH] [--after-object PATH]\n"
               "    [--before-skeleton-object PATH] [--after-skeleton-object PATH] [--position-tolerance N] "
               "[--out FILE]\n"
               "  cyuediff animation-frame ASSET --frame N [--object PATH] [--out FILE]\n"
               "  cyuediff skeleton ASSET [--project PROJECT] [--out FILE]\n"
               "  cyuediff attachments ASSET [--project PROJECT] [--out FILE]\n"
               "  cyuediff attachments-diff BEFORE AFTER [--out FILE]\n"
               "  cyuediff socket-pose ASSET [--object PATH] [--retarget-source KEY] [--out FILE]\n"
               "  cyuediff socket-pose-diff BEFORE AFTER [--before-object PATH] [--after-object PATH]\n"
               "    [--before-retarget-source KEY] [--after-retarget-source KEY] [--position-tolerance N]\n"
               "  cyuediff skeleton-animation ASSET [--project PROJECT] [--out FILE]\n"
               "  cyuediff skeleton-animation-diff BEFORE AFTER [--out FILE]\n"
               "  cyuediff skeleton-diff BEFORE AFTER [--out FILE]\n"
               "  cyuediff skeleton-pose ASSET [--object PATH] [--retarget-source KEY] [--out FILE]\n"
               "  cyuediff skeleton-pose-diff BEFORE AFTER [--before-object PATH] [--after-object PATH]\n"
               "    [--before-retarget-source KEY] [--after-retarget-source KEY] [--position-tolerance N] "
               "[--out FILE]\n"
               "  cyuediff collision ASSET [--project PROJECT] [--out FILE]\n"
               "  cyuediff collision-preview ASSET [--project PROJECT] [--out FILE]\n"
               "  cyuediff collision-diff BEFORE AFTER [--out FILE]\n"
               "  cyuediff material ASSET [--project PROJECT] [--out FILE]\n"
               "  cyuediff material-diff BEFORE AFTER [--out FILE]\n"
               "  cyuediff mesh ASSET [--project PROJECT] [--out FILE]\n"
               "  cyuediff geometry ASSET [--lod N] [--object NAME] [--codec MODULE.dll] [--out FILE]\n"
               "  cyuediff export-mesh ASSET --out MESH.obj [--lod N] [--object NAME] [--codec MODULE.dll]\n"
               "  cyuediff mesh-diff BEFORE AFTER [--lod N] [--object NAME] [--codec MODULE.dll] [--out "
               "FILE]\n"
               "  preview and image-diff also accept --codec MODULE.dll\n"
#ifdef CYUEDIFF_HAS_GIT
               "  cyuediff git-history REPO REV [--max-commits N] [--skip N] [--out FILE]\n"
               "  cyuediff git-catalog REPO REV [--max-assets N] [--max-entries N] [--out FILE]\n"
               "  cyuediff git-project REPO REV PROJECT [--engine-root ROOT] [--out FILE]\n"
               "  cyuediff git-inspect REPO REV PATH [--out FILE] [--lfs-objects DIR]\n"
               "  cyuediff git-diff REPO BEFORE AFTER PATH [--after-path PATH] [--out FILE]\n"
               "  Git commands also accept --lfs-objects DIR and --git-temp DIR.\n"
               "  git-inspect/git-diff accept --project REPO_PROJECT and long package names.\n"
               "  git-diff accepts --before-project/--after-project and "
               "--before-engine-root/--after-engine-root.\n"
               "  --no-engine skips engine discovery for Git project contexts.\n"
#endif
               "  cyuediff zen-animation INPUT.utoc ID --cooked-profile ue5.3 --animation-profile "
               "ue5.3-windows [context/dependency options]\n"
               "  cyuediff zen-animation-frame INPUT.utoc ID [same context] --frame 12.5\n"
               "  cyuediff zen-animation-frame-diff BEFORE.utoc ID AFTER.utoc ID [independent contexts] "
               "--before-frame N --after-frame N\n"
               "  cyuediff zen-animation-curves INPUT.utoc ID [animation context] --curve-settings-entry ID "
               "--time SECONDS\n"
               "  cyuediff zen-animation-curves-diff BEFORE.utoc ID AFTER.utoc ID [independent contexts] "
               "--before-time S --after-time S\n"
               "    --curve-settings-container TOC and "
               "--after-curve-settings-entry/--after-curve-settings-container select explicit dependencies.\n"
               "    --curve-default N selects the empty-curve fallback; --value-tolerance N applies to curve "
               "diffs.\n"
               "  cyuediff container INPUT.pak|INPUT.utoc [--out REPORT.json]\n"
               "  cyuediff container-read INPUT ENTRY_PATH_OR_ID --out BYTES.bin [--codec MODULE.dll]\n"
               "  cyuediff container-hash INPUT [--codec MODULE.dll] [--out REPORT.json]\n"
               "  cyuediff container-diff BEFORE AFTER [--codec MODULE.dll] [--out REPORT.json]\n"
               "  Container comparisons cover decoded bytes, not cooked object semantics.\n"
               "  cyuediff usmap MAPPINGS.usmap [--codec MODULE] [--out FILE]\n"
               "  cyuediff zen-inspect CONTAINER ENTRY --cooked-profile ue5.3|ue5.5 [--script-objects "
               "GLOBAL.utoc] [--codec MODULE.dll] [--out FILE]\n"
               "  cyuediff zen-diff BEFORE BEFORE_ENTRY AFTER AFTER_ENTRY --cooked-profile PROFILE "
               "[--after-cooked-profile PROFILE] [--out FILE]\n"
               "  cyuediff zen-catalog CONTAINER --cooked-profile PROFILE [--script-objects GLOBAL.utoc] "
               "[--codec MODULE.dll] [--out FILE]\n"
               "  cyuediff zen-skeletal CONTAINER ENTRY --skeletal-profile ue5.3-windows "
               "--keep-mobile-min-lod 0|1\n"
               "    [--has-vertex-colors 0|1] [--export-index N] [same cooked schema context]\n"
               "  cyuediff zen-skeletal-diff BEFORE ENTRY AFTER ENTRY [same skeletal context]\n"
               "    [--after-has-vertex-colors 0|1] [--lod N] [--after-lod N]\n"
               "  cyuediff zen-nanite CONTAINER ENTRY --nanite-profile ue5.3-windows [mesh/context flags]\n"
               "  cyuediff zen-nanite-cluster CONTAINER ENTRY --page N --cluster N [same flags]\n"
               "  cyuediff zen-nanite-diff BEFORE ENTRY AFTER ENTRY [independent mesh/Nanite/context flags]\n"
               "  Nanite inspection accepts --nanite-attributes 0|1; comparisons also accept "
               "--after-nanite-attributes.\n"
               "  zen-nanite-diff --compare-nanite-attributes 1 decodes and compares shading on common "
               "surfaces.\n"
               "  cyuediff zen-mesh CONTAINER ENTRY --mesh-profile ue5.3-windows --keep-mobile-min-lod 0|1\n"
               "  cyuediff zen-geometry CONTAINER ENTRY [--lod N] [--export-index N]\n"
               "  cyuediff zen-export-mesh CONTAINER ENTRY --out MESH.obj [--lod N]\n"
               "  cyuediff zen-mesh-diff BEFORE ENTRY AFTER ENTRY [--lod N] [--after-lod N]\n"
               "  Mesh commands also require --cooked-profile ue5.3 --schema CONTEXT.json.\n"
               "  cyuediff zen-physics CONTAINER ENTRY --physics-profile ue5.3-windows [cooked context]\n"
               "  cyuediff zen-physics-wire CONTAINER ENTRY [same explicit cooked physics context]\n"
               "  cyuediff zen-physics-diff BEFORE ENTRY AFTER ENTRY [same cooked context]\n"
               "  cyuediff physical-cloth ASSET --layouts CONTEXT.json --physical-cloth-profile "
               "ue5.3-windows [--include-values 0|1]\n"
               "  cyuediff physical-cloth-vertex|physical-cloth-transition|physical-cloth-wrap ASSET [same "
               "context] [--lod N] [--vertex N] [--transition up|down]\n"
               "  cyuediff export-physical-cloth ASSET [same context] [--lod N] --out MESH.obj\n"
               "  cyuediff physical-cloth-diff BEFORE AFTER [independent source contexts]\n"
               "  cyuediff zen-physical-cloth CONTAINER ENTRY --cooked-profile ue5.3 --schema CONTEXT.json "
               "--physical-cloth-profile ue5.3-windows [--include-values 0|1]\n"
               "  cyuediff zen-physical-cloth-vertex|zen-physical-cloth-transition CONTAINER ENTRY [same "
               "context] --lod N --vertex N [--transition up|down]\n"
               "  cyuediff zen-physical-cloth-wrap|zen-export-physical-cloth CONTAINER ENTRY [same context] "
               "--lod N [--transition up|down] [--out FILE]\n"
               "  cyuediff zen-physical-cloth-diff BEFORE ENTRY AFTER ENTRY [independent cooked contexts]\n"
               "  cyuediff zen-collision CONTAINER ENTRY --cooked-profile ue5.3 --schema CONTEXT.json\n"
               "  cyuediff zen-collision-wire CONTAINER ENTRY [same cooked context]\n"
               "  cyuediff zen-collision-diff BEFORE ENTRY AFTER ENTRY [same cooked context]\n"
               "  cyuediff zen-texture CONTAINER ENTRY --cooked-profile ue5.3 --schema CONTEXT.json\n"
               "  cyuediff zen-preview CONTAINER ENTRY --cooked-profile ue5.3 --schema CONTEXT.json --out "
               "IMAGE.png\n"
               "  cyuediff zen-image-diff BEFORE ENTRY AFTER ENTRY --cooked-profile ue5.3 --schema "
               "CONTEXT.json\n"
               "  cyuediff zen-hdr-diff BEFORE ENTRY AFTER ENTRY --cooked-profile ue5.3 --schema "
               "CONTEXT.json\n"
               "  Cooked pixel commands accept --export-index N, --platform N and --mip N.\n"
               "  Cooked pixel comparisons accept --after-export-index N, --after-platform N, --threshold "
               "and --heatmap.\n"
               "  zen-preview accepts --exposure EV for floating pixel display.\n"
               "  Cooked commands accept --schema CONTEXT.json; comparisons accept --after-schema.\n"
               "  zen-diff accepts --script-objects, --after-script-objects and --codec.\n"
               "  zen-inspect/zen-catalog accept --snapshot; zen-catalog accepts --max-packages and "
               "--max-exports.\n"
               "  cyuediff textconv ASSET\n"
               "  cyuediff catalog PROJECT [--include-engine] [--max-assets N] [--max-entries N] [--out "
               "FILE]\n"
               "  cyuediff project PROJECT [--engine-root ROOT]\n"
               "  cyuediff resolve PROJECT /Game/Package [--engine-root ROOT]\n"
               "Diff exit codes: 0 identical, 1 changes, 2 error/incomplete/unverified.\n";
        return 0;
    }
    if (args[0] == "effective-inspect" || args[0] == "effective-diff")
        return run_effective_command(args);
    if (is_binary_command(args[0]))
        return run_binary_command(args);
    std::vector<std::string> positional;
    Json flags = Json::object();
    const std::set<std::string> valued = {"--physical-cloth-profile",
                                          "--after-physical-cloth-profile",
                                          "--include-values",
                                          "--vertex",
                                          "--transition",
                                          "--compare-nanite-attributes",
                                          "--nanite-attributes",
                                          "--after-nanite-attributes",
                                          "--physics-profile",
                                          "--after-physics-profile",
                                          "--after-nanite-profile",
                                          "--nanite-profile",
                                          "--page",
                                          "--cluster",
                                          "--mesh-profile",
                                          "--after-mesh-profile",
                                          "--keep-mobile-min-lod",
                                          "--after-keep-mobile-min-lod",
                                          "--skeletal-profile",
                                          "--after-skeletal-profile",
                                          "--has-vertex-colors",
                                          "--after-has-vertex-colors",
                                          "--after-lod",
                                          "--animation-profile",
                                          "--after-animation-profile",
                                          "--interpolation",
                                          "--after-interpolation",
                                          "--bone-settings-container",
                                          "--bone-settings-entry",
                                          "--curve-settings-container",
                                          "--curve-settings-entry",
                                          "--after-curve-settings-container",
                                          "--after-curve-settings-entry",
                                          "--before-time",
                                          "--after-time",
                                          "--curve-default",
                                          "--after-bone-settings-container",
                                          "--after-bone-settings-entry",
                                          "--skeleton-container",
                                          "--skeleton-entry",
                                          "--after-skeleton-container",
                                          "--after-skeleton-entry",
                                          "--include-keys",
                                          "--before-frame",
                                          "--after-frame",
                                          "--rotation-tolerance",
                                          "--scale-tolerance",
                                          "--curve-family",
                                          "--curve-id",
                                          "--time-start",
                                          "--time-end",
                                          "--samples",
                                          "--before-lane",
                                          "--after-lane",
                                          "--value-tolerance",
                                          "--time",
                                          "--lane",
                                          "--frame",
                                          "--retarget-source",
                                          "--before-retarget-source",
                                          "--after-retarget-source",
                                          "--before-object",
                                          "--after-object",
                                          "--position-tolerance",
                                          "--max-packages",
                                          "--max-exports",
                                          "--max-properties",
                                          "--max-property-bytes",
                                          "--after-max-properties",
                                          "--after-max-property-bytes",
                                          "--layouts",
                                          "--after-layouts",
                                          "--payload-root",
                                          "--after-payload-root",
                                          "--schema",
                                          "--after-schema",
                                          "--cooked-profile",
                                          "--after-cooked-profile",
                                          "--script-objects",
                                          "--after-script-objects",
                                          "--max-assets",
                                          "--max-entries",
                                          "--exposure",
                                          "--export-index",
                                          "--after-export-index",
                                          "--platform",
                                          "--after-platform",
                                          "--project",
                                          "--before-project",
                                          "--after-project",
                                          "--engine-root",
                                          "--out",
                                          "--mip",
                                          "--object",
                                          "--threshold",
                                          "--heatmap",
                                          "--lod",
                                          "--codec",
                                          "--lfs-objects",
                                          "--git-temp",
                                          "--after-path",
                                          "--max-commits",
                                          "--skip",
                                          "--before-engine-root",
                                          "--after-engine-root",
                                          "--before-skeleton",
                                          "--after-skeleton",
                                          "--skeleton-object",
                                          "--before-skeleton-object",
                                          "--after-skeleton-object"};
    for (std::size_t i = 1; i < args.size(); ++i) {
        const auto &arg = args[i];
        if (valued.count(arg)) {
            if (i + 1 == args.size())
                throw Error("arguments", "Missing value for " + arg);
            flags[arg] = args[++i];
        } else if (arg == "--metadata" || arg == "--snapshot" || arg == "--no-engine" ||
                   arg == "--include-engine" || arg == "--name-identities")
            flags[arg] = true;
        else if (arg.compare(0, 2, "--") == 0)
            throw Error("arguments", "Unknown option " + arg);
        else
            positional.push_back(arg);
    }
    auto path_option = [&](const std::string &key) { return fs::u8path(flags.value(key, std::string{})); };
    const auto command = args[0];
    const bool source_socket = command == "animation-socket-pose" || command == "animation-socket-pose-diff";
    const bool source_pose_single = command == "animation-pose" || command == "animation-socket-pose";
    const bool source_pose_diff = command == "animation-pose-diff" || command == "animation-socket-pose-diff";
    auto required =
        (command == "physical-cloth-diff" || command == "diff" || command == "resolve" ||
         command == "image-diff" || source_pose_single || source_pose_diff ||
         command == "animation-frame-diff" || command == "curve-value-diff" || command == "curves-diff" ||
         command == "animation-diff" || command == "attachments-diff" || command == "socket-pose-diff" ||
         command == "skeleton-animation-diff" || command == "skeleton-pose-diff" ||
         command == "collision-diff" || command == "skeleton-diff" || command == "mesh-diff" ||
         command == "material-diff" || command == "hdr-diff" || command == "container-read" ||
         command == "container-diff")
            ? 2u
            : 1u;
    const bool git_command = command == "git-inspect" || command == "git-diff" || command == "git-project" ||
                             command == "git-catalog" || command == "git-history";
    const bool schema_command = command == "usmap";
    const bool source_types = command == "source-types";
    const bool cooked_physics =
        command == "zen-physics" || command == "zen-physics-wire" || command == "zen-physics-diff";
    const bool cooked_physics_diff = command == "zen-physics-diff";
    const bool cooked_collision =
        command == "zen-collision" || command == "zen-collision-wire" || command == "zen-collision-diff";
    const bool cooked_collision_diff = command == "zen-collision-diff";
    const bool source_physical = command == "physical-cloth" || command == "physical-cloth-vertex" ||
                                 command == "physical-cloth-transition" || command == "physical-cloth-wrap" ||
                                 command == "export-physical-cloth" || command == "physical-cloth-diff";
    const bool physical_cloth =
        command == "zen-physical-cloth" || command == "zen-physical-cloth-vertex" ||
        command == "zen-physical-cloth-transition" || command == "zen-physical-cloth-wrap" ||
        command == "zen-export-physical-cloth" || command == "zen-physical-cloth-diff";
    const bool physical_cloth_diff = command == "zen-physical-cloth-diff";
    const bool cooked_animation_curves =
        command == "zen-animation-curves" || command == "zen-animation-curves-diff";
    const bool cooked_animation_curve_diff = command == "zen-animation-curves-diff";
    const bool cooked_animation_frame_diff = command == "zen-animation-frame-diff";
    const bool cooked_animation = command == "zen-animation" || command == "zen-animation-frame" ||
                                  cooked_animation_frame_diff || cooked_animation_curves;
    const bool cooked_animation_diff = cooked_animation_frame_diff || cooked_animation_curve_diff;
    const bool cooked_skeletal = command == "zen-skeletal" || command == "zen-skeletal-diff";
    const bool cooked_skeletal_diff = command == "zen-skeletal-diff";
    const bool cooked_nanite =
        command == "zen-nanite" || command == "zen-nanite-cluster" || command == "zen-nanite-diff";
    const bool cooked_mesh = cooked_nanite || command == "zen-mesh" || command == "zen-geometry" ||
                             command == "zen-export-mesh" || command == "zen-mesh-diff";
    const bool cooked_mesh_diff = command == "zen-mesh-diff" || command == "zen-nanite-diff";
    const bool cooked_pixels = command == "zen-texture" || command == "zen-preview" || command == "zen-image-diff" || command == "zen-hdr-diff";
    const bool cooked_pixel_diff = command == "zen-image-diff" || command == "zen-hdr-diff";
    const bool cooked_compare = command == "zen-diff" || cooked_pixel_diff || cooked_mesh_diff ||
                                cooked_collision_diff || cooked_physics_diff || cooked_skeletal_diff ||
                                cooked_animation_diff || physical_cloth_diff;
    const bool zen_command = command == "zen-inspect" || command == "zen-diff" || command == "zen-catalog" ||
                             cooked_pixels || cooked_mesh || cooked_collision || cooked_physics ||
                             cooked_skeletal || cooked_animation || physical_cloth;
    if (zen_command)
        required = cooked_compare ? 4u : (command == "zen-catalog" ? 1u : 2u);
    const bool container_command = command == "container" || command == "container-read" ||
                                   command == "container-hash" || command == "container-diff";
    const bool catalog_command = command == "catalog" || command == "git-catalog";
    if (git_command)
        required =
            command == "git-diff" ? 4u : ((command == "git-catalog" || command == "git-history") ? 2u : 3u);
    if (source_types) {
        if (positional.size() < 4 || positional.size() > 1003)
            throw Error("arguments", "Select a mapping, exact archive versions and at least one source root");
        required = static_cast<unsigned>(positional.size());
    }
    if (positional.size() != required)
        throw Error("arguments", "Wrong number of arguments; use --help");
    const std::set<std::string> source_commands = {"physical-cloth",
                                                   "physical-cloth-vertex",
                                                   "physical-cloth-transition",
                                                   "physical-cloth-wrap",
                                                   "export-physical-cloth",
                                                   "physical-cloth-diff",
                                                   "inspect",
                                                   "textconv",
                                                   "diff",
                                                   "graph",
                                                   "texture",
                                                   "mesh",
                                                   "collision-preview",
                                                   "skeleton-animation",
                                                   "preview",
                                                   "image-diff",
                                                   "hdr-diff",
                                                   "material",
                                                   "material-diff",
                                                   "geometry",
                                                   "mesh-diff",
                                                   "export-mesh",
                                                   "curves",
                                                   "curves-diff",
                                                   "curve-value",
                                                   "curve-value-diff",
                                                   "curve-components",
                                                   "animation",
                                                   "animation-diff",
                                                   "animation-frame",
                                                   "animation-frame-diff",
                                                   "animation-pose",
                                                   "animation-pose-diff",
                                                   "animation-socket-pose",
                                                   "animation-socket-pose-diff",
                                                   "skeleton",
                                                   "skeleton-diff",
                                                   "skeleton-pose",
                                                   "skeleton-pose-diff",
                                                   "socket-pose",
                                                   "socket-pose-diff",
                                                   "attachments",
                                                   "attachments-diff",
                                                   "skeleton-animation-diff",
                                                   "collision",
                                                   "collision-diff",
                                                   "git-inspect",
                                                   "git-diff"};
    const bool source_read = source_commands.count(command) != 0;
    const bool source_compare =
        source_read &&
        (command == "diff" || (command.size() > 5 && command.compare(command.size() - 5, 5, "-diff") == 0));
    const bool image_command = command == "preview" || command == "image-diff" || command == "hdr-diff";
    const bool geometry_command = command == "geometry" || command == "export-mesh" || command == "mesh-diff";
    for (auto it = flags.begin(); it != flags.end(); ++it) {
        const auto &key = it.key();
        if (key == "--name-identities") {
            if ((command != "inspect" && command != "git-inspect" && command != "zen-inspect") ||
                flags.contains("--snapshot") || flags.contains("--metadata"))
                throw Error("arguments",
                            "Name identities require full inspect/git-inspect/zen-inspect output");
            continue;
        }
        if (key == "--max-properties" || key == "--max-property-bytes" || key == "--after-max-properties" ||
            key == "--after-max-property-bytes") {
            if (!source_read)
                throw Error("arguments", "Source read limits require a source decode command");
            if (key.rfind("--after-", 0) == 0 && !source_compare)
                throw Error("arguments", "Independent source limits require a source comparison");
            continue;
        }
        if (source_physical) {
            const bool common = key == "--out" || key == "--layouts" || key == "--project" ||
                                key == "--before-project" || key == "--engine-root" ||
                                key == "--physical-cloth-profile" || key == "--export-index";
            const bool pair =
                source_compare && (key == "--after-layouts" || key == "--after-project" ||
                                   key == "--after-physical-cloth-profile" || key == "--after-export-index");
            const bool lod = key == "--lod" && command != "physical-cloth" && !source_compare;
            const bool vertex = key == "--vertex" && (command == "physical-cloth-vertex" ||
                                                      command == "physical-cloth-transition");
            const bool transition = key == "--transition" && (command == "physical-cloth-transition" ||
                                                              command == "physical-cloth-wrap");
            if (!(common || pair || lod || vertex || transition ||
                  (key == "--include-values" && command == "physical-cloth")))
                throw Error("arguments", "Unsupported source physical cloth option: " + key);
            continue;
        }
        if (source_types) {
            if (key != "--out" && key != "--layouts" && key != "--codec")
                throw Error("arguments", "Unsupported source type preparation option: " + key);
            continue;
        }
        if ((key == "--physical-cloth-profile" || key == "--after-physical-cloth-profile" ||
             key == "--include-values" || key == "--vertex" || key == "--transition") &&
            !physical_cloth)
            throw Error("arguments", key + " requires a physical cloth command");
        if ((key == "--payload-root" || key == "--after-payload-root") && !image_command && !geometry_command)
            throw Error("arguments", key + " requires an image or geometry decode command");
        if (key == "--after-payload-root" && command != "image-diff" && command != "hdr-diff" && command != "mesh-diff")
            throw Error("arguments", "--after-payload-root requires a source comparison command");
        if (command == "layouts" && key != "--out")
            throw Error("arguments", "Layout catalogue inspection only accepts --out");
        if (key == "--layouts" && command != "inspect" && command != "diff" && command != "git-inspect" && command != "git-diff")
            throw Error("arguments", "--layouts requires inspect, diff, git-inspect or git-diff");
        if (key == "--after-layouts" && command != "diff" && command != "git-diff")
            throw Error("arguments", "--after-layouts requires diff or git-diff");
        if ((key == "--layouts" || key == "--after-layouts") && flags.contains("--metadata"))
            throw Error("arguments", "Binary layout descriptions require property reading");
        if (command == "git-history") {
            if (key != "--out" && key != "--git-temp" && key != "--max-commits" && key != "--skip")
                throw Error("arguments", "Unsupported history option: " + key);
            continue;
        }
        if (key=="--retarget-source" && command!="skeleton-pose" && command!="socket-pose")
            throw Error("arguments","--retarget-source requires skeleton-pose or socket-pose");
        if ((key=="--before-retarget-source"||key=="--after-retarget-source") && command!="skeleton-pose-diff" && command!="socket-pose-diff")
            throw Error("arguments",key+" requires skeleton-pose-diff or socket-pose-diff");
        if ((key == "--time-start" || key == "--time-end" || key == "--samples" || key == "--before-lane" ||
             key == "--after-lane" || key == "--value-tolerance") &&
            command != "curve-value-diff" && !(cooked_animation_curve_diff && key == "--value-tolerance"))
            throw Error("arguments",key+" requires curve-value-diff");
        if(key=="--curve-family" && command!="curves" && command!="curves-diff" && command!="curve-value" && command!="curve-value-diff" && command!="curve-components")throw Error("arguments","--curve-family requires a curve command");
        if(key=="--curve-id" && command!="curve-components")throw Error("arguments","--curve-id requires curve-components");
        if (key == "--time" && command != "curve-value" && command != "curve-components" &&
            command != "zen-animation-curves")
            throw Error("arguments", "--time requires curve-value or curve-components");
        if(key=="--lane" && command!="curve-value")throw Error("arguments","--lane requires curve-value");
        if (key == "--frame" && command != "animation-frame" && command != "zen-animation-frame" &&
            !source_pose_single)
            throw Error("arguments", "--frame requires animation-frame");
        if ((key == "--before-frame" || key == "--after-frame") && command != "animation-frame-diff" &&
            !cooked_animation_frame_diff && !source_pose_diff)
            throw Error("arguments", key + " requires animation-frame-diff or animation-pose-diff");
        if ((key == "--rotation-tolerance" || key == "--scale-tolerance") &&
            command != "animation-frame-diff" && !cooked_animation_frame_diff)
            throw Error("arguments", key + " requires animation-frame-diff");
        if(key=="--skeleton-object" && !source_pose_single)throw Error("arguments",key+" requires animation-pose");
        if((key=="--before-skeleton" || key=="--after-skeleton" || key=="--before-skeleton-object" || key=="--after-skeleton-object") && !source_pose_diff)throw Error("arguments",key+" requires animation-pose-diff");
        if ((key == "--before-object" || key == "--after-object") && command != "curve-value-diff" && command != "curves-diff" && command != "animation-diff" && command != "animation-frame-diff" && !source_pose_diff &&
            command != "skeleton-pose-diff" && command != "socket-pose-diff")
            throw Error("arguments", key + " requires a model or pose comparison");
        if (key == "--position-tolerance" && command != "skeleton-pose-diff" &&
            command != "socket-pose-diff" && command != "animation-frame-diff" &&
            !cooked_animation_frame_diff && !source_pose_diff)
            throw Error("arguments","--position-tolerance requires a pose comparison");
        if (key == "--max-commits" || key == "--skip")
            throw Error("arguments", key + " requires git-history");
        if (schema_command) {
            if (key != "--out" && key != "--codec")
                throw Error("arguments", "Unsupported USMAP option: " + key);
            continue;
        }
        if ((key == "--skeletal-profile" || key == "--after-skeletal-profile" || key == "--has-vertex-colors" || key == "--after-has-vertex-colors") && !cooked_skeletal)
            throw Error("arguments", key + " requires a cooked skeletal command");
        if (zen_command) {
            if (physical_cloth) {
                const bool common = key == "--out" || key == "--codec" || key == "--cooked-profile" ||
                                    key == "--script-objects" || key == "--schema" ||
                                    key == "--physical-cloth-profile" || key == "--export-index";
                const bool pair = physical_cloth_diff &&
                                  (key == "--after-cooked-profile" || key == "--after-script-objects" ||
                                   key == "--after-schema" || key == "--after-physical-cloth-profile" ||
                                   key == "--after-export-index");
                const bool lod = key == "--lod" && command != "zen-physical-cloth" && !physical_cloth_diff;
                const bool vertex = key == "--vertex" && (command == "zen-physical-cloth-vertex" ||
                                                          command == "zen-physical-cloth-transition");
                const bool transition =
                    key == "--transition" &&
                    (command == "zen-physical-cloth-transition" || command == "zen-physical-cloth-wrap");
                if (!(common || pair || lod || vertex || transition ||
                      (key == "--include-values" && command == "zen-physical-cloth")))
                    throw Error("arguments", "Unsupported physical cloth option: " + key);
                continue;
            }
            if (key != "--out" && key != "--codec" && key != "--cooked-profile" &&
                key != "--script-objects" && key != "--schema" &&
                !(key == "--snapshot" && (command == "zen-inspect" || command == "zen-catalog")) &&
                !(command == "zen-catalog" && (key == "--max-packages" || key == "--max-exports")) &&
                !((cooked_pixels || cooked_mesh || cooked_skeletal || cooked_animation) &&
                  key == "--export-index") &&
                !(cooked_animation &&
                  (key == "--animation-profile" || key == "--interpolation" ||
                   key == "--bone-settings-container" || key == "--bone-settings-entry" ||
                   key == "--curve-settings-container" || key == "--curve-settings-entry" ||
                   key == "--skeleton-container" || key == "--skeleton-entry")) &&
                !(command == "zen-animation" && key == "--include-keys") &&
                !(command == "zen-animation-frame" && key == "--frame") &&
                !(cooked_animation_diff &&
                  (key == "--after-animation-profile" || key == "--after-interpolation" ||
                   key == "--after-bone-settings-container" || key == "--after-bone-settings-entry" ||
                   key == "--after-curve-settings-container" || key == "--after-curve-settings-entry" ||
                   key == "--after-skeleton-container" || key == "--after-skeleton-entry" ||
                   key == "--after-export-index")) &&
                !(cooked_animation_frame_diff &&
                  (key == "--before-frame" || key == "--after-frame" || key == "--position-tolerance" ||
                   key == "--rotation-tolerance" || key == "--scale-tolerance")) &&
                !(cooked_animation_curves && key == "--curve-default") &&
                !(command == "zen-animation-curves" && key == "--time") &&
                !(cooked_animation_curve_diff &&
                  (key == "--before-time" || key == "--after-time" || key == "--value-tolerance")) &&
                !(cooked_physics && key == "--physics-profile") &&
                !(cooked_physics_diff && key == "--after-physics-profile") &&
                !(cooked_skeletal && (key == "--skeletal-profile" || key == "--has-vertex-colors" ||
                                      key == "--keep-mobile-min-lod")) &&
                !(cooked_skeletal_diff &&
                  (key == "--after-skeletal-profile" || key == "--after-has-vertex-colors" ||
                   key == "--after-keep-mobile-min-lod" || key == "--after-export-index" || key == "--lod" ||
                   key == "--after-lod")) &&
                !(cooked_mesh && (key == "--mesh-profile" || key == "--keep-mobile-min-lod")) &&
                !((cooked_nanite || command == "zen-mesh") &&
                  (key == "--nanite-profile" || key == "--nanite-attributes")) &&
                !(command == "zen-nanite-diff" &&
                  (key == "--after-nanite-profile" || key == "--after-nanite-attributes" ||
                   key == "--compare-nanite-attributes")) &&
                !(command == "zen-nanite-cluster" && (key == "--page" || key == "--cluster")) &&
                !(cooked_mesh && !cooked_nanite && command != "zen-mesh" && key == "--lod") &&
                !(cooked_mesh_diff &&
                  (key == "--after-mesh-profile" || key == "--after-keep-mobile-min-lod" ||
                   (key == "--after-lod" && !cooked_nanite) || key == "--after-export-index")) &&
                !(cooked_pixels && command != "zen-texture" && (key == "--mip" || key == "--platform")) &&
                !(command == "zen-preview" && key == "--exposure") &&
                !(cooked_pixel_diff && (key == "--threshold" || key == "--heatmap" ||
                                        key == "--after-platform" || key == "--after-export-index")) &&
                !(cooked_compare && (key == "--after-cooked-profile" || key == "--after-script-objects" ||
                                     key == "--after-schema")))
                throw Error("arguments", "Unsupported cooked option: " + key);
            continue;
        }
        if (key == "--animation-profile" || key == "--after-animation-profile" || key == "--interpolation" ||
            key == "--after-interpolation" || key == "--include-keys" || key == "--bone-settings-container" ||
            key == "--bone-settings-entry" || key == "--after-bone-settings-container" ||
            key == "--after-bone-settings-entry" || key == "--curve-settings-container" ||
            key == "--curve-settings-entry" || key == "--after-curve-settings-container" ||
            key == "--after-curve-settings-entry" || key == "--before-time" || key == "--after-time" ||
            key == "--curve-default" || key == "--skeleton-container" || key == "--skeleton-entry" ||
            key == "--after-skeleton-container" || key == "--after-skeleton-entry")
            throw Error("arguments", key + " requires a cooked animation command");
        if (key == "--physics-profile" || key == "--after-physics-profile")
            throw Error("arguments", key + " requires a cooked physics command");
        if ((key == "--nanite-profile" || key == "--after-nanite-profile" || key == "--nanite-attributes" ||
             key == "--after-nanite-attributes" || key == "--compare-nanite-attributes" || key == "--page" ||
             key == "--cluster") &&
            !cooked_mesh)
            throw Error("arguments", key + " requires a Nanite inspection command");
        if (key == "--mesh-profile" || key == "--after-mesh-profile" || key == "--keep-mobile-min-lod" ||
            key == "--after-keep-mobile-min-lod" || key == "--after-lod")
            throw Error("arguments", key + " requires a cooked mesh command");
        if (key == "--export-index" || key == "--after-export-index" || key == "--platform" || key == "--after-platform")
            throw Error("arguments", key + " requires a cooked texture command");
        if (key == "--schema" || key == "--after-schema" || key == "--cooked-profile" ||
            key == "--after-cooked-profile" || key == "--script-objects" || key == "--after-script-objects" ||
            key == "--max-packages" || key == "--max-exports")
            throw Error("arguments", key + " requires a cooked command");
        if (container_command) {
            if (key != "--out" && !(key == "--codec" && command != "container"))
                throw Error("arguments", "Unsupported container option: " + key);
            continue;
        }
        const bool side_context = key == "--before-project" || key == "--after-project" ||
                                  key == "--before-engine-root" || key == "--after-engine-root";
        if ((key == "--max-assets" || key == "--max-entries") && !catalog_command)
            throw Error("arguments", key + " requires a catalogue command");
        if (key == "--include-engine" && command != "catalog")
            throw Error("arguments", "--include-engine requires catalog");
        if (catalog_command && key != "--out" && key != "--max-assets" && key != "--max-entries" &&
            !(command == "catalog" && (key == "--engine-root" || key == "--include-engine")) &&
            !(command == "git-catalog" && key == "--git-temp"))
            throw Error("arguments", "Unsupported catalogue option: " + key);
        if (git_command && !catalog_command && key != "--out" && key != "--lfs-objects" &&
            key != "--git-temp" && key != "--engine-root" && key != "--no-engine" &&
            !(key == "--layouts" && (command == "git-inspect" || command == "git-diff")) &&
            !(key == "--after-layouts" && command == "git-diff") &&
            !(key == "--project" && command != "git-project") &&
            !(command == "git-diff" && (side_context || key == "--after-path")))
            throw Error("arguments", "Unsupported option for Git inspection: " + key);
        if (!git_command &&
            (key == "--lfs-objects" || key == "--git-temp" || key == "--after-path" || key == "--no-engine" ||
             key == "--before-engine-root" || key == "--after-engine-root"))
            throw Error("arguments", key + " requires a Git command");
        if (((key == "--mip") && !image_command) ||
            (key == "--object" && !image_command && !geometry_command && command != "skeleton-pose" && command != "socket-pose" && command != "animation-frame" && !source_pose_single && command != "curve-value" && command != "curve-components"))
            throw Error("arguments", key + " requires an image or geometry command");
        if ((key == "--threshold" || key == "--heatmap") && command != "image-diff" && command != "hdr-diff")
            throw Error("arguments", key + " requires image-diff or hdr-diff");
        if (key == "--exposure" && command != "preview")
            throw Error("arguments", "--exposure requires preview");
        if ((key == "--lod" && !geometry_command) ||
            (key == "--codec" && !geometry_command && !image_command))
            throw Error("arguments", key + " requires a source decode command");
        if ((image_command || geometry_command) && (key == "--metadata" || key == "--snapshot"))
            throw Error("arguments", key + " cannot be used with image decoding");
    }
    auto integer_option = [&](const std::string &key, std::uint32_t limit) {
        auto text = flags.value(key, std::string("0"));
        std::uint64_t value = 0;
        if (text.empty())
            throw Error("arguments", "Empty value for " + key);
        for (char c : text) {
            if (c < '0' || c > '9' || value > limit / 10 ||
                (value == limit / 10 && static_cast<unsigned>(c - '0') > limit % 10))
                throw Error("arguments", "Invalid or out of range value for " + key);
            value = value * 10 + static_cast<unsigned>(c - '0');
        }
        if (value > limit)
            throw Error("arguments", "Out of range value for " + key);
        return static_cast<std::uint32_t>(value);
    };
    SourceReadLimits before_limits;
    if (flags.contains("--max-properties"))
        before_limits.max_properties = integer_option("--max-properties", SourceReadLimits::maximum_work);
    if (flags.contains("--max-property-bytes"))
        before_limits.max_property_bytes = integer_option(
            "--max-property-bytes", static_cast<std::uint32_t>(SourceReadLimits::maximum_bytes));
    before_limits.validate();
    auto after_limits = before_limits;
    if (flags.contains("--after-max-properties"))
        after_limits.max_properties =
            integer_option("--after-max-properties", SourceReadLimits::maximum_work);
    if (flags.contains("--after-max-property-bytes"))
        after_limits.max_property_bytes = integer_option(
            "--after-max-property-bytes", static_cast<std::uint32_t>(SourceReadLimits::maximum_bytes));
    after_limits.validate();
    auto real_option = [&](const std::string &key, double fallback, double minimum, double maximum) {
        if (!flags.contains(key))
            return fallback;
        std::istringstream input(flags.at(key).get<std::string>());
        input.imbue(std::locale::classic());
        double value = 0;
        input >> value;
        if (!input || input.peek() != std::char_traits<char>::eof() || !std::isfinite(value) ||
            value < minimum || value > maximum)
            throw Error("arguments", "Invalid or out of range value for " + key);
        return value;
    };
    auto catalog_options = [&] {
        CatalogOptions result;
        result.include_engine = flags.contains("--include-engine");
        if (flags.contains("--max-assets"))
            result.max_assets = integer_option("--max-assets", 1'000'000);
        if (flags.contains("--max-entries"))
            result.max_entries = integer_option("--max-entries", 10'000'000);
        return result;
    };
    auto select_texture = [&](const Package &package) -> const TextureSource & {
        auto name = flags.value("--object", std::string{});
        const TextureSource *selected = nullptr;
        for (const auto &texture : package.textures) {
            auto leaf = texture.object_path.substr(texture.object_path.find_last_of('.') + 1);
            if (!name.empty() && name != texture.object_path && name != leaf)
                continue;
            if (selected)
                throw Error("texture-selection",
                            "Several textures match; specify their full path with --object");
            selected = &texture;
        }
        if (!selected)
            throw Error("texture-selection", "No matching decoded Texture2D source in this package");
        return *selected;
    };
    auto root = path_option("--engine-root");
    std::vector<fs::path> protected_files, protected_directories;
    auto load_layouts = [&](const char *key) -> std::shared_ptr<const BinaryLayouts> {
        if (!flags.contains(key)) return {};
        const auto path = path_option(key);
        protected_files.push_back(path);
        return std::make_shared<const BinaryLayouts>(BinaryLayouts::read(path));
    };
    const auto before_layouts = load_layouts("--layouts");
    const auto after_layouts = flags.contains("--after-layouts") ? load_layouts("--after-layouts") : before_layouts;
    const auto before_decoders = before_layouts ? before_layouts->decoder_registry() : nullptr;
    const auto after_decoders = after_layouts == before_layouts ? before_decoders :
        (after_layouts ? after_layouts->decoder_registry() : nullptr);
    auto read = [&](const std::string &input, const std::string &project_key, ReadOptions options) {
        const auto &selected_layouts = project_key == "--after-project" ? after_layouts : before_layouts;
        options.decoders = project_key == "--after-project" ? after_decoders : before_decoders;
        (project_key == "--after-project" ? after_limits : before_limits).apply(options);
        options.narrow_string_encoding = selected_layouts ? selected_layouts->narrow_string_encoding() : NarrowStringEncoding::unspecified;
        auto project = path_option(project_key);
        if (project.empty())
            project = path_option("--project");
        fs::path asset = fs::u8path(input);
        if (!project.empty()) {
            auto context = ProjectContext::open(project, root);
            protected_files.push_back(context.project_file);
            if (input.rfind("/Game/", 0) == 0 || input.rfind("/Engine/", 0) == 0 ||
                (!fs::exists(asset) && input[0] == '/'))
                asset = context.resolve(input);
        }
        protected_files.push_back(asset);
        for (const auto *ext : {".uexp", ".ubulk", ".uptnl", ".m.ubulk", ".o.ubulk", ".upayload"}) {
            auto f = asset;
            f.replace_extension(ext);
            protected_files.push_back(f);
        }
        return Package::open(asset, options);
    };
    BlockDecoder decoder;
    auto codec = path_option("--codec");
    if (!codec.empty()) {
        decoder = load_block_decoder(codec);
        protected_files.push_back(codec);
    }
    TextureDecodeOptions texture_options;
    texture_options.decoder = decoder;
    MeshDecodeOptions mesh_options;
    mesh_options.decoder = decoder;
    texture_options.virtualized_payload_root = path_option("--payload-root");
    mesh_options.virtualized_payload_root = texture_options.virtualized_payload_root;
    auto after_texture_options = texture_options;
    auto after_mesh_options = mesh_options;
    if (flags.contains("--after-payload-root")) {
        after_texture_options.virtualized_payload_root = path_option("--after-payload-root");
        after_mesh_options.virtualized_payload_root = after_texture_options.virtualized_payload_root;
    }
    for (const auto &root : {texture_options.virtualized_payload_root, after_texture_options.virtualized_payload_root})
        if (!root.empty()) protected_directories.push_back(root);
    Json output;
    std::optional<DiffResult> streamed_diff;
#ifdef CYUEDIFF_HAS_GIT
    std::optional<GitComparison> streamed_git;
#endif
    std::vector<std::uint8_t> raw_bytes;
    fs::path raw_destination;
    MeshGeometry obj;
    fs::path obj_destination;
    ImageRgba8 png;
    fs::path png_destination;
    int exit_code = 0;
    ReadOptions options;
    options.value_identities.names = flags.contains("--name-identities");
    before_limits.apply(options);
    options.decoders = before_decoders;
    options.narrow_string_encoding = before_layouts ? before_layouts->narrow_string_encoding() : NarrowStringEncoding::unspecified;
    auto after_read_options = options;
    after_limits.apply(after_read_options);
    after_read_options.decoders = after_decoders;
    after_read_options.narrow_string_encoding = after_layouts ? after_layouts->narrow_string_encoding() : NarrowStringEncoding::unspecified;
    auto physical_options = [&](bool after, PhysicalClothOptions fallback) {
        const std::string key = after ? "--after-physical-cloth-profile" : "--physical-cloth-profile";
        if (flags.contains(key)) {
            if (flags.at(key).get<std::string>() != "ue5.3-windows")
                throw Error("arguments", "Supported physical cloth profile: ue5.3-windows");
            fallback.profile = PhysicalClothProfile::unreal_5_3_windows;
        }
        if (fallback.profile == PhysicalClothProfile::unspecified)
            throw Error("arguments", "Select --physical-cloth-profile ue5.3-windows");
        return fallback;
    };
    auto selected_physical_export = [&](const char *key) -> std::size_t {
        return flags.contains(key) ? integer_option(key, 1'000'000) : SIZE_MAX;
    };
    auto physical_output = [&](const PhysicalClothAsset &before) {
        const auto physical_command = command.rfind("zen-", 0) == 0 ? command.substr(4) : command;
        if (physical_command == "physical-cloth") {
            output = before.to_json(flags.contains("--include-values") &&
                                    integer_option("--include-values", 1) != 0);
            exit_code = before.available() ? 0 : 2;
        } else {
            const auto lod = static_cast<std::uint32_t>(integer_option("--lod", 127));
            if (physical_command == "export-physical-cloth") {
                if (!before.available() || lod >= before.lods().size() || !before.lods()[lod].geometry)
                    throw Error("physical-cloth-unavailable", "Physical geometry is unavailable");
                obj = *before.lods()[lod].geometry;
                obj_destination = path_option("--out");
                if (obj_destination.empty())
                    throw Error("arguments", "Physical OBJ export requires --out");
                output = {{"schema", "cyuediff.physical-cloth-export/1"},
                          {"object", before.object_path()},
                          {"lod", lod},
                          {"vertices", obj.vertices.size()},
                          {"triangles", obj.triangles.size()}};
            } else if (physical_command == "physical-cloth-vertex") {
                output = {{"schema", "cyuediff.physical-cloth-vertex/1"},
                          {"object", before.object_path()},
                          {"lod", lod},
                          {"value", before.vertex(lod, static_cast<std::uint32_t>(
                                                           integer_option("--vertex", UINT32_MAX)))}};
            } else {
                const auto direction = flags.value("--transition", std::string("up"));
                if (direction != "up" && direction != "down")
                    throw Error("arguments", "Select --transition up or down");
                const bool up = direction == "up";
                if (physical_command == "physical-cloth-transition")
                    output = {{"schema", "cyuediff.physical-cloth-transition/1"},
                              {"object", before.object_path()},
                              {"lod", lod},
                              {"direction", direction},
                              {"value", before.transition(lod, up,
                                                          static_cast<std::uint32_t>(
                                                              integer_option("--vertex", UINT32_MAX)))}};
                else
                    output = {{"schema", "cyuediff.physical-cloth-wrap/1"},
                              {"object", before.object_path()},
                              {"lod", lod},
                              {"direction", direction},
                              {"scope", "Chaos-5.3-stored-source-pose-transition"},
                              {"simulationEvaluated", false},
                              {"positions", before.wrap_transition(lod, up)}};
            }
        }
        output["provenance"] = before.to_json().at("provenance");
    };
    if (command == "layouts") {
        const auto input = fs::u8path(positional[0]);
        protected_files.push_back(input);
        const auto catalog = BinaryLayouts::read(input);
        output = {{"schema", "cyuediff.binary-layout-catalog/1"}, {"sha256", catalog.sha256()},
                  {"decoderCount", catalog.decoder_registry()->size()}, {"definition", catalog.to_json()}};
    } else if (source_types) {
        auto version = [&](const std::string &text) {
            std::uint32_t value = 0;
            if (text.empty())
                throw Error("arguments", "Empty source archive version");
            for (const char c : text) {
                if (c < '0' || c > '9' || value > (static_cast<std::uint32_t>(INT32_MAX) - (c - '0')) / 10)
                    throw Error("arguments", "Source archive version must be a nonnegative int32");
                value = value * 10 + static_cast<unsigned>(c - '0');
            }
            return static_cast<std::int32_t>(value);
        };
        SerializationProfile source_profile;
        source_profile.ue4 = {version(positional[1]), version(positional[1])};
        source_profile.ue5 = {version(positional[2]), version(positional[2])};
        const auto input = fs::u8path(positional[0]);
        protected_files.push_back(input);
        SchemaReadOptions schema_options;
        if (decoder)
            schema_options.decoder = container_block_decoder(decoder);
        const auto schemas = UnversionedSchemas::read(input, schema_options);
        const std::vector<std::string> roots(positional.begin() + 3, positional.end());
        const auto types = TaggedTypes::from_schemas(schemas, roots, source_profile);
        output = before_layouts ? before_layouts->to_json()
                                : Json{{"format", "cyuediff.binary-layouts/1"}, {"layouts", Json::object()}};
        if (output.contains("taggedTypes"))
            throw Error("tagged-types",
                        "Base catalogue already has source type metadata; select an unmodified base");
        output["taggedTypes"] = types.to_json();
        BinaryLayouts::parse(output);
    } else if (schema_command) {
        auto input = fs::u8path(positional[0]);
        protected_files.push_back(input);
        SchemaReadOptions schema_options;
        if (decoder)
            schema_options.decoder = container_block_decoder(decoder);
        output = UnversionedSchemas::read(input, schema_options).to_json();
    } else if (zen_command) {
        auto profile = [&](const std::string &key, ZenProfile fallback) {
            if (!flags.contains(key))
                return fallback;
            auto value = flags.at(key).get<std::string>();
            if (value == "ue5.3")
                return ZenProfile::unreal_5_3;
            if (value == "ue5.5")
                return ZenProfile::unreal_5_5;
            throw Error("zen-profile", "Supported cooked profiles are ue5.3 and ue5.5");
        };
        ZenReadOptions before_options;
        before_options.value_identities.names = flags.contains("--name-identities");
        before_options.profile = profile("--cooked-profile", ZenProfile::unspecified);
        if (before_options.profile == ZenProfile::unspecified)
            throw Error("zen-profile", "Cooked reads require --cooked-profile");
        ContainerReadOptions read_options;
        if (decoder)
            read_options.decoder = container_block_decoder(decoder);
        auto open = [&](const fs::path &path) {
            auto container = Container::open(path);
            for (const auto &file : container.files())
                protected_files.push_back(file);
            return container;
        };
        auto scripts = [&](const fs::path &path) -> std::shared_ptr<const ScriptObjects> {
            if (path.empty())
                return {};
            auto container = open(path);
            std::size_t found = container.entries().size();
            for (std::size_t j = 0; j < container.entries().size(); ++j)
                if (container.entries()[j].chunk_type == 5) {
                    if (found != container.entries().size())
                        throw Error("zen-entry", "Ambiguous global script object table");
                    found = j;
                }
            if (found == container.entries().size())
                throw Error("zen-entry", "No script object chunk in the selected container");
            return std::make_shared<ScriptObjects>(
                ScriptObjects::parse(container.read(found, read_options), before_options));
        };
        auto select = [&](const Container &container, const std::string &key) {
            std::size_t found = container.entries().size();
            for (std::size_t j = 0; j < container.entries().size(); ++j) {
                const auto &entry = container.entries()[j];
                if (entry.id == key || (!entry.path.empty() && entry.path == key)) {
                    if (found != container.entries().size())
                        throw Error("zen-entry", "Ambiguous cooked entry");
                    found = j;
                }
            }
            if (found == container.entries().size())
                throw Error("zen-entry", "No matching cooked entry");
            return found;
        };
        auto schema_context = [&](const fs::path &path) -> std::shared_ptr<const ZenSchemaContext> {
            if (path.empty())
                return {};
            auto manifest = ZenSchemaManifest::read(path);
            protected_files.push_back(manifest.source_path());
            protected_files.push_back(manifest.mapping_path());
            SchemaReadOptions options;
            options.decoder = read_options.decoder;
            return manifest.load(options);
        };
        before_options.script_objects = scripts(path_option("--script-objects"));
        before_options.schema_context = schema_context(path_option("--schema"));
        auto input = open(fs::u8path(positional[0]));
        if (command == "zen-catalog") {
            ZenCatalogOptions catalog_options;
            catalog_options.include_snapshots = flags.contains("--snapshot");
            if (flags.contains("--max-packages"))
                catalog_options.max_packages = integer_option("--max-packages", 1'000'000);
            if (flags.contains("--max-exports"))
                catalog_options.max_exports = integer_option("--max-exports", 1'000'000);
            output = catalog_zen_packages(input, before_options, read_options, catalog_options);
            if (!output.at("complete").get<bool>())
                exit_code = 2;
        } else if (cooked_animation) {
            auto native_options = [&](bool after, const Container &source, const ZenReadOptions &z,
                                      CookedAnimationOptions fallback) {
                const auto prefix = after ? std::string("--after-") : std::string("--");
                auto value = [&](const std::string &suffix) {
                    return flags.value(prefix + suffix, flags.value("--" + suffix, std::string{}));
                };
                if (!value("animation-profile").empty()) {
                    if (value("animation-profile") != "ue5.3-windows")
                        throw Error("arguments", "Supported cooked animation profile: ue5.3-windows");
                    fallback.profile = CookedAnimationProfile::unreal_5_3_windows;
                }
                if (!value("interpolation").empty()) {
                    const auto mode = value("interpolation");
                    if (mode != "linear" && mode != "step")
                        throw Error("arguments", "Animation interpolation must be linear or step");
                    fallback.interpolation = mode == "linear" ? CompressedAnimationInterpolation::linear
                                                              : CompressedAnimationInterpolation::step;
                }
                auto dependency = [&](const std::string &kind) -> std::optional<CookedAnimationDependency> {
                    const auto entry = value(kind + "-entry");
                    const auto path = flags.value(prefix + kind + "-container", std::string{});
                    if (!path.empty() && entry.empty())
                        throw Error("arguments", "A dependency container requires its explicit entry");
                    if (entry.empty())
                        return std::nullopt;
                    auto dependency_container = path.empty() ? source : open(fs::u8path(path));
                    return CookedAnimationDependency{dependency_container,
                                                     select(dependency_container, entry), z, read_options};
                };
                fallback.curve_settings = dependency("curve-settings");
                fallback.bone_settings = dependency("bone-settings");
                fallback.skeleton = dependency("skeleton");
                return fallback;
            };
            auto selected_export = [&](const char *key) -> std::size_t {
                return flags.contains(key) ? integer_option(key, 1'000'000) : SIZE_MAX;
            };
            auto opts = native_options(false, input, before_options, {});
            const auto before = read_zen_animation(input, select(input, positional[1]), before_options,
                                                   read_options, opts, selected_export("--export-index"));
            if (command == "zen-animation")
                output = before.to_json(flags.contains("--include-keys") &&
                                        integer_option("--include-keys", 1) != 0);
            else if (command == "zen-animation-curves") {
                output =
                    before.sample_curves(real_option("--time", 0, -std::numeric_limits<float>::max(),
                                                     std::numeric_limits<float>::max()),
                                         real_option("--curve-default", 0, -std::numeric_limits<float>::max(),
                                                     std::numeric_limits<float>::max()));
                output["curveContext"] = before.curve_information();
                exit_code = output.value("ready", false) ? 0 : 2;
            } else if (command == "zen-animation-frame") {
                output = before.sample(real_option("--frame", 0, 0, 2147483646.));
                if (!output.value("ready", false))
                    exit_code = 2;
            } else {
                auto after_options = before_options;
                after_options.profile = profile("--after-cooked-profile", before_options.profile);
                if (flags.contains("--after-script-objects"))
                    after_options.script_objects = scripts(path_option("--after-script-objects"));
                if (flags.contains("--after-schema"))
                    after_options.schema_context = schema_context(path_option("--after-schema"));
                auto other = open(fs::u8path(positional[2]));
                const auto after =
                    read_zen_animation(other, select(other, positional[3]), after_options, read_options,
                                       native_options(true, other, after_options, opts),
                                       selected_export("--after-export-index"));
                if (cooked_animation_curve_diff) {
                    CookedAnimationCurveDiffOptions comparison;
                    comparison.before_time =
                        real_option("--before-time", 0, -std::numeric_limits<float>::max(),
                                    std::numeric_limits<float>::max());
                    comparison.after_time = real_option("--after-time", 0, -std::numeric_limits<float>::max(),
                                                        std::numeric_limits<float>::max());
                    comparison.value_tolerance = real_option("--value-tolerance", 0, 0, 1e300);
                    comparison.empty_default =
                        real_option("--curve-default", 0, -std::numeric_limits<float>::max(),
                                    std::numeric_limits<float>::max());
                    output = compare_cooked_animation_curves(before, after, comparison);
                    output["beforeCurveContext"] = before.curve_information();
                    output["afterCurveContext"] = after.curve_information();
                    exit_code = !output.value("complete", false)     ? 2
                                : output.at("status") == "identical" ? 0
                                                                     : 1;
                } else {
                    AnimationFrameDiffOptions comparison;
                    comparison.position_tolerance = real_option("--position-tolerance", 0, 0, 1e300);
                    comparison.rotation_tolerance_degrees = real_option("--rotation-tolerance", 0, 0, 180);
                    comparison.scale_tolerance = real_option("--scale-tolerance", 0, 0, 1e300);
                    output = compare_cooked_animation_frames(
                        before, after, real_option("--before-frame", 0, 0, 2147483646.),
                        real_option("--after-frame", 0, 0, 2147483646.), comparison);
                    output["beforeAnimation"] = before.to_json();
                    output["afterAnimation"] = after.to_json();
                    exit_code = !output.value("complete", false)     ? 2
                                : output.at("status") == "identical" ? 0
                                                                     : 1;
                }
            }
        } else if (cooked_skeletal) {
            auto native_options = [&](bool after, CookedSkeletalOptions fallback) {
                const auto prefix = after ? std::string("--after-") : std::string("--");
                const auto profile_key = prefix + "skeletal-profile",
                           mobile_key = prefix + "keep-mobile-min-lod",
                           color_key = prefix + "has-vertex-colors";
                if (flags.contains(profile_key)) {
                    if (flags.at(profile_key).get<std::string>() != "ue5.3-windows")
                        throw Error("arguments", "Supported cooked skeletal profile: ue5.3-windows");
                    fallback.profile = CookedSkeletalProfile::unreal_5_3_windows;
                }
                if (flags.contains(mobile_key))
                    fallback.keep_mobile_min_lod_setting = integer_option(mobile_key, 1) != 0;
                if (flags.contains(color_key))
                    fallback.has_vertex_colors = integer_option(color_key, 1) != 0;
                return fallback;
            };
            auto selected_export = [&](const char *key) -> std::size_t {
                return flags.contains(key) ? integer_option(key, 1'000'000) : SIZE_MAX;
            };
            auto opts = native_options(false, {});
            auto before = read_zen_skeletal_mesh(input, select(input, positional[1]), before_options,
                                                 read_options, opts, selected_export("--export-index"));
            if (!cooked_skeletal_diff)
                output = before.to_json(true);
            else {
                auto after_options = before_options;
                after_options.profile = profile("--after-cooked-profile", before_options.profile);
                if (flags.contains("--after-script-objects"))
                    after_options.script_objects = scripts(path_option("--after-script-objects"));
                if (flags.contains("--after-schema"))
                    after_options.schema_context = schema_context(path_option("--after-schema"));
                auto other = open(fs::u8path(positional[2]));
                auto after = read_zen_skeletal_mesh(other, select(other, positional[3]), after_options,
                                                    read_options, native_options(true, opts),
                                                    selected_export("--after-export-index"));
                const auto lod = integer_option("--lod", 1'000'000),
                           after_lod =
                               flags.contains("--after-lod") ? integer_option("--after-lod", 1'000'000) : lod;
                if (lod >= before.lods.size() || after_lod >= after.lods.size() ||
                    !before.lods[lod].geometry || !after.lods[after_lod].geometry)
                    throw Error("cooked-skeletal-unavailable",
                                "Selected skeletal LOD geometry is unavailable");
                auto result = compare_meshes(*before.lods[lod].geometry, *after.lods[after_lod].geometry);
                output = result.to_json();
                exit_code = !result.complete ? 2 : result.changes.empty() ? 0 : 1;
                output["beforeMesh"] = before.to_json();
                output["afterMesh"] = after.to_json();
                output["beforeLod"] = lod;
                output["afterLod"] = after_lod;
            }
        } else if (cooked_mesh) {
            auto mesh_options = [&](const char *profile_key, const char *setting_key,
                                    CookedMeshOptions fallback) {
                if (flags.contains(profile_key)) {
                    if (flags.at(profile_key).get<std::string>() != "ue5.3-windows")
                        throw Error("arguments", "Supported cooked mesh profile: ue5.3-windows");
                    fallback.profile = CookedMeshProfile::unreal_5_3_windows;
                }
                if (flags.contains(setting_key))
                    fallback.keep_mobile_min_lod_setting = integer_option(setting_key, 1) != 0;
                return fallback;
            };
            auto selected_export = [&](const char *key) -> std::size_t {
                return flags.contains(key) ? integer_option(key, 1'000'000) : SIZE_MAX;
            };
            auto opts = mesh_options("--mesh-profile", "--keep-mobile-min-lod", {});
            opts.geometry = command != "zen-mesh" && !cooked_nanite;
            if (flags.contains("--nanite-profile")) {
                if (flags.at("--nanite-profile") != "ue5.3-windows")
                    throw Error("arguments", "Supported Nanite profile: ue5.3-windows");
                opts.nanite.profile = NaniteProfile::unreal_5_3_windows;
            }
            if (flags.contains("--compare-nanite-attributes"))
                opts.nanite.attributes = integer_option("--compare-nanite-attributes", 1) != 0;
            if (flags.contains("--nanite-attributes")) {
                if (opts.nanite.profile == NaniteProfile::unspecified)
                    throw Error("arguments", "Attribute decoding requires an explicit Nanite profile");
                opts.nanite.attributes = integer_option("--nanite-attributes", 1) != 0;
            }
            if (cooked_nanite && opts.nanite.profile == NaniteProfile::unspecified)
                throw Error("arguments", "Nanite commands require --nanite-profile ue5.3-windows");
            auto before = read_zen_static_mesh(input, select(input, positional[1]), before_options,
                                               read_options, opts, selected_export("--export-index"));
            auto geometry = [&](const CookedStaticMesh &mesh, std::size_t lod) -> const MeshGeometry & {
                if (lod >= mesh.lods.size() || !mesh.lods[lod].geometry)
                    throw Error("cooked-mesh-unavailable",
                                "Selected cooked mesh LOD geometry is unavailable");
                return *mesh.lods[lod].geometry;
            };
            const auto lod = integer_option("--lod", 1'000'000);
            if (command == "zen-nanite")
                output = before.nanite->catalog();
            else if (command == "zen-nanite-cluster") {
                const auto model = before.nanite->geometry();
                if (!model)
                    throw Error(before.nanite->geometry_known() ? "cooked-nanite-empty"
                                                                : "cooked-nanite-unavailable",
                                "Nanite cluster geometry is unavailable or empty");
                const auto page = integer_option("--page", 16383), cluster = integer_option("--cluster", 255);
                const auto metadata = model->cluster_metadata(page, cluster);
                output = {{"metadata", metadata},
                          {"positions", Json::array()},
                          {"quantizedPositions", Json::array()},
                          {"triangles", Json::array()},
                          {"naniteContext", before.nanite->information()}};
                for (std::size_t i = 0; i < metadata.at("vertices").get<std::size_t>(); ++i) {
                    output["positions"].push_back(model->position(page, cluster, i));
                    if (model->attributes_decoded())
                        output["attributes"].push_back(model->attributes(page, cluster, i).to_json());
                    output["quantizedPositions"].push_back(model->quantized_position(page, cluster, i));
                }
                for (std::size_t i = 0; i < metadata.at("triangles").get<std::size_t>(); ++i) {
                    const auto triangle = model->triangle(page, cluster, i);
                    output["triangles"].push_back(
                        {{"vertices", triangle.vertices}, {"material", triangle.material}});
                }
            } else if (command == "zen-nanite-diff") {
                auto after_options = before_options;
                after_options.profile = profile("--after-cooked-profile", before_options.profile);
                if (flags.contains("--after-script-objects"))
                    after_options.script_objects = scripts(path_option("--after-script-objects"));
                if (flags.contains("--after-schema"))
                    after_options.schema_context = schema_context(path_option("--after-schema"));
                auto after_mesh_options =
                    mesh_options("--after-mesh-profile", "--after-keep-mobile-min-lod", opts);
                if (flags.contains("--after-nanite-profile") &&
                    flags.at("--after-nanite-profile") != "ue5.3-windows")
                    throw Error("arguments", "Unsupported after Nanite profile");
                if (flags.contains("--after-nanite-attributes"))
                    after_mesh_options.nanite.attributes =
                        integer_option("--after-nanite-attributes", 1) != 0;
                auto other = open(fs::u8path(positional[2]));
                const auto after =
                    read_zen_static_mesh(other, select(other, positional[3]), after_options, read_options,
                                         after_mesh_options, selected_export("--after-export-index"));
                NaniteSurfaceDiffOptions comparison;
                comparison.attributes = flags.contains("--compare-nanite-attributes") &&
                                        integer_option("--compare-nanite-attributes", 1) != 0;
                const auto result = compare_nanite_surfaces(*before.nanite, *after.nanite, comparison);
                output = result.to_json();
                output["beforeContext"] = before.nanite->information();
                output["afterContext"] = after.nanite->information();
                exit_code = !output.at("complete").get<bool>()   ? 2
                            : output.at("status") == "identical" ? 0
                                                                 : 1;
            } else if (command == "zen-mesh")
                output = before.to_json();
            else if (command == "zen-geometry") {
                output = geometry(before, lod).to_json();
                output["cookedMesh"] = before.to_json();
                output["lod"] = lod;
            } else if (command == "zen-export-mesh") {
                obj_destination = path_option("--out");
                if (obj_destination.empty())
                    throw Error("arguments", "zen-export-mesh requires --out MESH.obj");
                obj = geometry(before, lod);
                output = {{"exported", true}, {"cookedMesh", before.to_json()}, {"lod", lod}};
            } else {
                auto after_options = before_options;
                after_options.profile = profile("--after-cooked-profile", before_options.profile);
                if (flags.contains("--after-script-objects"))
                    after_options.script_objects = scripts(path_option("--after-script-objects"));
                if (flags.contains("--after-schema"))
                    after_options.schema_context = schema_context(path_option("--after-schema"));
                auto other = open(fs::u8path(positional[2]));
                auto after = read_zen_static_mesh(
                    other, select(other, positional[3]), after_options, read_options,
                    mesh_options("--after-mesh-profile", "--after-keep-mobile-min-lod", opts),
                    selected_export("--after-export-index"));
                const auto after_lod =
                    flags.contains("--after-lod") ? integer_option("--after-lod", 1'000'000) : lod;
                auto result = compare_meshes(geometry(before, lod), geometry(after, after_lod));
                output = result.to_json();
                exit_code = result.changes.empty() ? 0 : 1;
                output["beforeMesh"] = before.to_json();
                output["afterMesh"] = after.to_json();
                output["beforeLod"] = lod;
                output["afterLod"] = after_lod;
            }
        } else if (cooked_physics) {
            auto physics_options = [&](const char *key, CookedPhysicsOptions fallback) {
                if (flags.contains(key)) {
                    if (flags.at(key).get<std::string>() != "ue5.3-windows")
                        throw Error("arguments", "Supported cooked physics profile: ue5.3-windows");
                    fallback.profile = CookedPhysicsProfile::unreal_5_3_windows;
                }
                return fallback;
            };
            auto options = physics_options("--physics-profile", {});
            auto before = read_zen_cooked_physics(input, select(input, positional[1]), before_options,
                                                  read_options, options);
            if (!cooked_physics_diff) {
                if (command == "zen-physics-wire") {
                    auto review = cooked_physics_review(before, options);
                    output = review.wire.to_json();
                    output["provenance"] = before.at("provenance");
                    exit_code = before.at("complete").get<bool>() ? 0 : 2;
                } else {
                    output = std::move(before);
                    exit_code = output.at("complete").get<bool>() ? 0 : 2;
                }
            } else {
                auto after_options = before_options;
                after_options.profile = profile("--after-cooked-profile", before_options.profile);
                if (flags.contains("--after-script-objects"))
                    after_options.script_objects = scripts(path_option("--after-script-objects"));
                if (flags.contains("--after-schema"))
                    after_options.schema_context = schema_context(path_option("--after-schema"));
                auto other = open(fs::u8path(positional[2]));
                auto after =
                    read_zen_cooked_physics(other, select(other, positional[3]), after_options, read_options,
                                            physics_options("--after-physics-profile", options));
                output = compare_cooked_physics(before, after, options);
                output["beforeContext"] = before.at("provenance");
                output["afterContext"] = after.at("provenance");
                exit_code = !output.at("complete").get<bool>() ? 2 : output.at("changes").empty() ? 0 : 1;
            }
        } else if (physical_cloth) {
            const auto opts = physical_options(false, {});
            const auto package =
                read_zen_package(input, select(input, positional[1]), before_options, read_options);
            const auto before =
                read_physical_cloth(package, opts, selected_physical_export("--export-index"));
            if (physical_cloth_diff) {
                auto after_options = before_options;
                after_options.profile = profile("--after-cooked-profile", before_options.profile);
                if (flags.contains("--after-script-objects"))
                    after_options.script_objects = scripts(path_option("--after-script-objects"));
                if (flags.contains("--after-schema"))
                    after_options.schema_context = schema_context(path_option("--after-schema"));
                const auto other = open(fs::u8path(positional[2]));
                const auto after_package =
                    read_zen_package(other, select(other, positional[3]), after_options, read_options);
                const auto after = read_physical_cloth(after_package, physical_options(true, opts),
                                                       selected_physical_export("--after-export-index"));
                PhysicalClothComparisonOptions compare;
                compare.explicit_object_pair = true;
                output = compare_physical_cloth(before, after, compare);
                exit_code = !output.at("complete").get<bool>() ? 2 : output.at("changes").empty() ? 0 : 1;
            } else
                physical_output(before);
        } else if (cooked_collision) {
            auto before = read_zen_package(input, select(input, positional[1]), before_options, read_options);
            if (command == "zen-collision")
                output = collision_snapshot(before);
            else if (command == "zen-collision-wire") {
                output = collision_wireframe(before).to_json();
                output["provenance"] = collision_snapshot(before).at("provenance");
            } else {
                auto after_options = before_options;
                after_options.profile = profile("--after-cooked-profile", before_options.profile);
                if (flags.contains("--after-script-objects"))
                    after_options.script_objects = scripts(path_option("--after-script-objects"));
                if (flags.contains("--after-schema"))
                    after_options.schema_context = schema_context(path_option("--after-schema"));
                auto other = open(fs::u8path(positional[2]));
                auto after =
                    read_zen_package(other, select(other, positional[3]), after_options, read_options);
                auto comparison = compare_collisions(before, after);
                output = comparison.to_json();
                output["beforeContext"] = collision_snapshot(before).at("provenance");
                output["afterContext"] = collision_snapshot(after).at("provenance");
                exit_code = !comparison.complete ? 2 : comparison.changes.empty() ? 0 : 1;
            }
        } else if (cooked_pixels) {
            auto selected_export = [&](const char *key) -> std::size_t {
                return flags.contains(key) ? integer_option(key, 1'000'000) : SIZE_MAX;
            };
            auto before = read_zen_texture2d(input, select(input, positional[1]), before_options, read_options,
                                            {}, selected_export("--export-index"));
            const auto mip = integer_option("--mip", 31);
            auto selected_platform = [&](const CookedTexture2D &texture, const char *key) -> const CookedTexturePlatform & {
                if (!flags.contains(key) && texture.platforms.size() != 1)
                    throw Error("cooked-texture-selection", "Select a cooked pixel format with --platform/--after-platform");
                const auto index = integer_option(key, 15);
                if (index >= texture.platforms.size())
                    throw Error("cooked-texture-selection", "Cooked pixel format index exceeds platform table");
                return texture.platforms[index];
            };
            if (command == "zen-texture") output = before.to_json();
            else if (command == "zen-preview") {
                const auto &platform = selected_platform(before, "--platform");
                const bool hdr = gpu_pixel_format_is_hdr(unreal_gpu_pixel_format(platform.format));
                const auto exposure = real_option("--exposure", 0, -32, 32);
                if (!hdr && flags.contains("--exposure")) throw Error("arguments", "--exposure requires floating cooked samples");
                png = hdr ? tone_map_hdr(decode_cooked_mip_hdr(platform, mip), exposure) : decode_cooked_mip(platform, mip);
                png_destination = path_option("--out");
                if (png_destination.empty()) throw Error("arguments", "zen-preview requires --out IMAGE.png");
                output = {{"schema", "cyuediff.cooked-preview/1"}, {"texture", before.to_json()},
                          {"mip", mip}, {"width", png.width}, {"height", png.height},
                          {"conversion", hdr ? "explicit HDR display tone map" : "RGBA8 stored channels; no color transform"}};
                if (hdr) output["exposureEV"] = exposure;
            } else {
                auto after_options = before_options;
                after_options.profile = profile("--after-cooked-profile", before_options.profile);
                if (flags.contains("--after-script-objects")) after_options.script_objects = scripts(path_option("--after-script-objects"));
                if (flags.contains("--after-schema")) after_options.schema_context = schema_context(path_option("--after-schema"));
                auto other = open(fs::u8path(positional[2]));
                auto after = read_zen_texture2d(other, select(other, positional[3]), after_options, read_options,
                                               {}, selected_export("--after-export-index"));
                const auto &a = selected_platform(before, "--platform");
                const auto &b = selected_platform(after, "--after-platform");
                png_destination = path_option("--heatmap");
                if (command == "zen-hdr-diff") {
                    auto comparison = compare_hdr_images(decode_cooked_mip_hdr(a,mip), decode_cooked_mip_hdr(b,mip),
                        real_option("--threshold",0,0,std::numeric_limits<double>::max()), !png_destination.empty());
                    output = comparison.to_json();exit_code = comparison.comparable && !comparison.changed_pixels ? 0 : 1;
                    png = std::move(comparison.heatmap);
                } else {
                    auto comparison = compare_images(decode_cooked_mip(a,mip), decode_cooked_mip(b,mip),
                        static_cast<std::uint8_t>(integer_option("--threshold",255)), !png_destination.empty());
                    output = comparison.to_json();exit_code = comparison.comparable && !comparison.changed_pixels ? 0 : 1;
                    png = std::move(comparison.heatmap);
                }
                output["mip"] = mip;output["beforeTexture"] = before.to_json();output["afterTexture"] = after.to_json();
            }
        } else {
            auto before = read_zen_package(input, select(input, positional[1]), before_options, read_options);
            if (command == "zen-inspect")
                output = flags.contains("--snapshot") ? before.snapshot() : before.to_json();
            else {
                auto after_options = before_options;
                after_options.profile = profile("--after-cooked-profile", before_options.profile);
                if (flags.contains("--after-script-objects"))
                    after_options.script_objects = scripts(path_option("--after-script-objects"));
                if (flags.contains("--after-schema"))
                    after_options.schema_context = schema_context(path_option("--after-schema"));
                auto other = open(fs::u8path(positional[2]));
                auto after =
                    read_zen_package(other, select(other, positional[3]), after_options, read_options);
                output = compare_zen_packages(before, after);
                exit_code = output.at("status") == "identical" ? 0 : 1;
            }
        }
    } else if (container_command) {
        auto input = Container::open(fs::u8path(positional[0]));
        for (const auto &file : input.files())
            protected_files.push_back(file);
        ContainerReadOptions decode_options;
        if (decoder)
            decode_options.decoder = container_block_decoder(decoder);
        if (command == "container-diff") {
            auto after = Container::open(fs::u8path(positional[1]));
            for (const auto &file : after.files())
                protected_files.push_back(file);
            output = compare_containers(input, after, decode_options);
            exit_code = output.at("status") == "identical" ? 0 : 1;
        } else if (command == "container-read") {
            raw_destination = path_option("--out");
            if (raw_destination.empty())
                throw Error("arguments", "container-read requires --out");
            std::size_t found = input.entries().size();
            for (std::size_t j = 0; j < input.entries().size(); ++j) {
                const auto &e = input.entries()[j];
                if (e.id == positional[1] || (!e.path.empty() && e.path == positional[1])) {
                    if (found != input.entries().size())
                        throw Error("container-entry", "Ambiguous member name");
                    found = j;
                }
            }
            if (found == input.entries().size())
                throw Error("container-entry", "No matching container member");
            raw_bytes = input.read(found, decode_options);
            output = {{"schema", "cyuediff.container-read/1"},
                      {"entry", input.entries()[found].to_json()},
                      {"size", raw_bytes.size()},
                      {"sha256", sha256_bytes(raw_bytes)},
                      {"coverage", "decoded-bytes"}};
        } else {
            output = input.to_json();
            if (command == "container-hash") {
                output["decodedSha256"] = Json::array();
                for (std::size_t j = 0; j < input.entries().size(); ++j) {
                    const auto &e = input.entries()[j];
                    if (!e.deleted)
                        output["decodedSha256"].push_back(
                            {{"id", e.id}, {"sha256", sha256_bytes(input.read(j, decode_options))}});
                }
            }
        }
    } else if (git_command) {
#ifdef CYUEDIFF_HAS_GIT
        GitOptions git_options;
        git_options.temporary_root = path_option("--git-temp");
        git_options.lfs_objects = path_option("--lfs-objects");
        auto repo = GitRepository::open(fs::u8path(positional[0]), git_options);
        protected_directories.push_back(repo.common_directory());
        protected_directories.push_back(repo.lfs_objects());
        protected_files.push_back(repo.root() / ".git");
        auto protect = [&](const std::string &path) {
            auto asset = repo.root() / fs::u8path(path);
            protected_files.push_back(asset);
            for (const auto *ext : {".uexp", ".ubulk", ".uptnl", ".m.ubulk", ".o.ubulk", ".upayload"}) {
                auto companion = asset;
                companion.replace_extension(ext);
                protected_files.push_back(companion);
            }
        };
        auto project_options = [&](const std::string &side) {
            GitProjectOptions result;
            result.engine_root = path_option(side);
            if (result.engine_root.empty())
                result.engine_root = root;
            result.discover_engine = !flags.contains("--no-engine");
            if (!result.discover_engine && !result.engine_root.empty())
                throw Error("arguments", "--no-engine conflicts with an explicit engine root");
            return result;
        };
        auto protect_context = [&](const GitProjectContext &context) {
            for (const auto &document : context.documents)
                protected_files.push_back(document.file.blob_oid.empty()
                                              ? fs::u8path(document.file.path)
                                              : repo.root() / fs::u8path(document.file.path));
            if (!context.engine.root.empty())
                protected_directories.push_back(context.engine.root / "Engine");
        };
        auto protect_revision = [&](const GitRevision &revision) {
            if (revision.storage == "git")
                protect(revision.repository_path);
            else
                for (const auto &file : revision.files)
                    protected_files.push_back(fs::u8path(file.path));
        };
        if (command == "git-history") {
            GitHistoryOptions history_options;
            if (flags.contains("--max-commits"))
                history_options.max_commits = integer_option("--max-commits", 1000);
            if (flags.contains("--skip"))
                history_options.skip = integer_option("--skip", 1'000'000);
            output = repo.history(positional[1], history_options).to_json();
        } else if (command == "git-catalog") {
            auto catalog = repo.catalog(positional[1], catalog_options());
            for (const auto &asset : catalog.assets)
                protect(asset.path);
            output = catalog.to_json();
            if (!catalog.complete)
                exit_code = 2;
        } else if (command == "git-project") {
            auto context = repo.project(positional[1], positional[2], project_options("--engine-root"));
            protect_context(context);
            output = context.to_json();
            if (!context.complete)
                exit_code = 2;
        } else if (command == "git-inspect") {
            GitRevision revision;
            auto project = flags.value("--project", std::string{});
            Json context = nullptr;
            if (!project.empty()) {
                auto selected = repo.project(positional[1], project, project_options("--engine-root"));
                protect_context(selected);
                revision = repo.materialize(selected, positional[2]);
                context = selected.to_json();
                if (!selected.complete)
                    exit_code = 2;
            } else {
                if (flags.contains("--engine-root") || flags.contains("--no-engine"))
                    throw Error("arguments", "Engine selection requires a Git project context");
                revision = repo.materialize(positional[1], positional[2]);
            }
            protect_revision(revision);
            auto package = revision.open(options);
            output = package.to_json();
            output["revision"] = revision.to_json();
            if (!context.is_null())
                output["projectContext"] = context;
            if (!package.complete_input)
                exit_code = 2;
        } else {
            auto project = flags.value("--project", std::string{});
            auto a = flags.value("--before-project", project), b = flags.value("--after-project", project);
            GitComparison comparison;
            if (!a.empty() || !b.empty()) {
                if (a.empty() || b.empty())
                    throw Error("arguments", "Specify a historical project for both sides");
                auto before = repo.project(positional[1], a, project_options("--before-engine-root"));
                auto after = repo.project(positional[2], b, project_options("--after-engine-root"));
                protect_context(before);
                protect_context(after);
                comparison = compare_git(repo, before, after, positional[3],
                                         flags.value("--after-path", std::string{}), options, after_read_options);
            } else {
                if (flags.contains("--engine-root") || flags.contains("--before-engine-root") ||
                    flags.contains("--after-engine-root") || flags.contains("--no-engine"))
                    throw Error("arguments", "Engine selection requires historical project contexts");
                comparison = compare_git(repo, positional[1], positional[2], positional[3],
                                         flags.value("--after-path", std::string{}), options, after_read_options);
            }
            protect_revision(comparison.before);
            protect_revision(comparison.after);
            const auto status = comparison.status();
            streamed_git = std::move(comparison);
            exit_code =
                status == "identical" ? 0 : ((status == "incomplete" || status == "unverified") ? 2 : 1);
        }
#else
        throw Error("git-unavailable", "Configure CYUEDIFF_BUILD_GIT=ON to enable Git commands");
#endif
    } else if (command == "curve-value-diff" || command == "curves" || command == "curves-diff" ||
               command == "curve-value" || command == "curve-components") {
        CurveOptions curve_options;
        const auto family=flags.value("--curve-family",std::string("float"));
        if(family=="transform")curve_options.animation_family=AnimationCurveFamily::Transform;
        else if(family!="float")throw Error("arguments","--curve-family must be float or transform");
        if(command=="curve-value-diff"){
        if(flags.contains("--metadata") || flags.contains("--snapshot") || !flags.contains("--time-start") || !flags.contains("--time-end"))
            throw Error("arguments","curve-value-diff requires decoded properties, --time-start and --time-end");
        CurveValueDiffOptions selected;
        selected.curves=curve_options;
        selected.time_begin=real_option("--time-start",0,-std::numeric_limits<double>::max(),std::numeric_limits<double>::max());
        selected.time_end=real_option("--time-end",1,-std::numeric_limits<double>::max(),std::numeric_limits<double>::max());
        selected.value_tolerance=real_option("--value-tolerance",0,0,std::numeric_limits<double>::max());
        if(flags.contains("--samples"))selected.samples=integer_option("--samples",4096);
        selected.before_object=flags.value("--before-object",std::string{});selected.after_object=flags.value("--after-object",std::string{});
        selected.before_lane=flags.value("--before-lane",std::string{});selected.after_lane=flags.value("--after-lane",std::string{});
        const auto before=read(positional[0],"--before-project",options),after=read(positional[1],"--after-project",options);
        output=compare_curve_values(before,after,selected);exit_code=!output.at("complete").get<bool>()?2:output.at("changedSamples").get<std::size_t>()?1:0;
        } else {
        if(flags.contains("--metadata") || flags.contains("--snapshot"))throw Error("arguments","Curve inspection requires decoded properties");
        if((command=="curve-value" || command=="curve-components") && !flags.contains("--time"))throw Error("arguments",command+" requires --time");
        const auto before=read(positional[0],command=="curves-diff"?"--before-project":"--project",options);
        if(command=="curves-diff"){
            const auto after=read(positional[1],"--after-project",options);
            const auto comparison=compare_curves(before,after,flags.value("--before-object",std::string{}),flags.value("--after-object",std::string{}),curve_options);
            output=comparison.to_json();exit_code=!comparison.complete?2:comparison.changes.empty()?0:1;
        }else if(command=="curve-components") {
            output=evaluate_curve_components(before,real_option("--time",0,-std::numeric_limits<double>::max(),std::numeric_limits<double>::max()),flags.value("--object",std::string{}),flags.value("--curve-id",std::string{}),curve_options);
            if(!output.at("ready").get<bool>())exit_code=2;
        }else if(command=="curve-value") {
            output=evaluate_curve(before,real_option("--time",0,-std::numeric_limits<double>::max(),std::numeric_limits<double>::max()),
                                  flags.value("--object",std::string{}),flags.value("--lane",std::string{}),curve_options);
            if(!output.at("ready").get<bool>())exit_code=2;
        }else output=curve_snapshot(before,curve_options);
        if(!before.complete_input)exit_code=2;
        }
    } else if (source_pose_single || source_pose_diff) {
        const bool comparing = source_pose_diff;
        if(flags.contains("--metadata") || flags.contains("--snapshot"))throw Error("arguments","Animation poses require decoded properties");
        if(!comparing) {
            if(!flags.contains("--frame"))throw Error("arguments",command+" requires --frame");
            const auto frame=integer_option("--frame",INT32_MAX);
            const auto animation=read(positional[0],"--project",options),skeleton=read(positional[1],"--project",options);
            const auto prepared=animation_pose(animation,skeleton,frame,flags.value("--object",std::string{}),flags.value("--skeleton-object",std::string{}));
            output=source_socket?animation_socket_pose(prepared,skeleton).to_json():prepared.to_json();
            exit_code=output.at("complete").get<bool>()?0:2;
        } else {
            for(const auto *required_flag:{"--before-skeleton","--after-skeleton","--before-frame","--after-frame"})
                if(!flags.contains(required_flag)||flags.at(required_flag).get<std::string>().empty())throw Error("arguments",command+" requires "+required_flag);
            AnimationPoseDiffOptions selected;selected.position_tolerance=real_option("--position-tolerance",0,0,std::numeric_limits<double>::max());
            AnimationPoseSelection a,b;a.frame=integer_option("--before-frame",INT32_MAX);b.frame=integer_option("--after-frame",INT32_MAX);
            a.animation_object=flags.value("--before-object",std::string{});b.animation_object=flags.value("--after-object",std::string{});
            a.skeleton_object=flags.value("--before-skeleton-object",std::string{});b.skeleton_object=flags.value("--after-skeleton-object",std::string{});
            const auto before=read(positional[0],"--before-project",options),after=read(positional[1],"--after-project",options);
            const auto before_skeleton=read(flags.at("--before-skeleton").get<std::string>(),"--before-project",options),after_skeleton=read(flags.at("--after-skeleton").get<std::string>(),"--after-project",options);
            if(source_socket) {
                const auto ap=animation_pose(before,before_skeleton,a.frame,a.animation_object,a.skeleton_object,selected.pose);
                const auto bp=animation_pose(after,after_skeleton,b.frame,b.animation_object,b.skeleton_object,selected.pose);
                output=compare_animation_socket_poses(animation_socket_pose(ap,before_skeleton),animation_socket_pose(bp,after_skeleton),selected);
            } else output=compare_animation_poses(before,before_skeleton,after,after_skeleton,a,b,selected);
            exit_code=output.at("status")=="identical"?0:output.at("status")=="changed"?1:2;
        }
    } else if (command == "animation-frame-diff") {
        if(flags.contains("--metadata") || flags.contains("--snapshot") || !flags.contains("--before-frame") || !flags.contains("--after-frame"))throw Error("arguments","animation-frame-diff requires decoded properties, --before-frame and --after-frame");
        AnimationFrameDiffOptions selected;
        selected.position_tolerance=real_option("--position-tolerance",0,0,std::numeric_limits<double>::max());
        selected.rotation_tolerance_degrees=real_option("--rotation-tolerance",0,0,180);
        selected.scale_tolerance=real_option("--scale-tolerance",0,0,std::numeric_limits<double>::max());
        const auto af=integer_option("--before-frame",INT32_MAX),bf=integer_option("--after-frame",INT32_MAX);
        const auto before=read(positional[0],"--before-project",options),after=read(positional[1],"--after-project",options);
        output=compare_animation_frames(before,after,af,bf,flags.value("--before-object",std::string{}),flags.value("--after-object",std::string{}),selected);
        exit_code=output["status"]=="identical"?0:output["status"]=="changed"?1:2;
    } else if (command == "animation" || command == "animation-frame" || command == "animation-diff") {
        if(flags.contains("--metadata") || flags.contains("--snapshot"))
            throw Error("arguments","Animation source inspection requires decoded properties");
        if(command=="animation-frame" && !flags.contains("--frame"))
            throw Error("arguments","animation-frame requires --frame");
        const auto before=read(positional[0],command=="animation-diff"?"--before-project":"--project",options);
        if(command=="animation-diff") {
            const auto after=read(positional[1],"--after-project",options);
            const auto comparison=compare_animation_models(before,after,flags.value("--before-object",std::string{}),
                                                           flags.value("--after-object",std::string{}));
            output=comparison.to_json();exit_code=!comparison.complete?2:comparison.changes.empty()?0:1;
        }else if(command=="animation-frame")
            output=animation_frame(before,integer_option("--frame",INT32_MAX),flags.value("--object",std::string{}));
        else output=animation_snapshot(before);
        if(!before.complete_input)exit_code=2;
    } else if (source_physical) {
        const auto opts = physical_options(false, {});
        const auto package = read(positional[0], "--before-project", options);
        const auto before = read_physical_cloth(package, opts, selected_physical_export("--export-index"));
        if (source_compare) {
            const auto after_package = read(positional[1], "--after-project", options);
            const auto after = read_physical_cloth(after_package, physical_options(true, opts),
                                                   selected_physical_export("--after-export-index"));
            PhysicalClothComparisonOptions compare;
            compare.explicit_object_pair = true;
            output = compare_physical_cloth(before, after, compare);
            exit_code = !output.at("complete").get<bool>() ? 2 : output.at("changes").empty() ? 0 : 1;
        } else
            physical_output(before);
    } else if (command == "inspect" || command == "textconv" || command == "graph" || command == "texture" ||
               command == "mesh" || command == "material" || command == "collision" ||
               command == "collision-preview" || command == "skeleton" || command == "skeleton-animation" ||
               command == "skeleton-pose" || command == "attachments" || command == "socket-pose") {
        if ((command == "graph" || command == "texture" || command == "mesh" || command == "material" ||
             command == "collision" || command == "collision-preview" || command == "skeleton" ||
             command == "skeleton-pose" || command == "skeleton-animation" || command=="attachments" || command=="socket-pose") &&
            flags.contains("--metadata"))
            throw Error("arguments", "Graph/texture inspection requires properties");
        if (flags.contains("--metadata"))
            options.properties = options.fingerprints = false;
        auto package = read(positional[0], "--project", options);
        if (command == "textconv" && package.source_read_limit_failed)
            throw Error("source-read-limits",
                        "A source read limit/count guard failed; text conversion is unavailable");
        if (command == "graph")
            output = package.blueprint_snapshot();
        else if (command == "texture")
            output = package.texture_snapshot();
        else if (command == "mesh")
            output = package.mesh_snapshot();
        else if (command == "material")
            output = package.material_snapshot();
        else if (command == "skeleton")
            output = package.skeleton_snapshot();
        else if (command == "skeleton-animation")
            output = skeleton_animation_snapshot(package);
        else if(command=="attachments")output=attachment_snapshot(package);
        else if(command=="socket-pose"){
            ReferencePoseOptions pose_options;pose_options.retarget_source=flags.value("--retarget-source",std::string{});
            output=socket_pose(package,flags.value("--object",std::string{}),pose_options).to_json();
        }
        else if (command == "skeleton-pose") {
            ReferencePoseOptions pose_options;pose_options.retarget_source=flags.value("--retarget-source",std::string{});
            output = reference_pose(package, flags.value("--object",std::string{}),pose_options).to_json();
        }
        else if (command == "collision-preview")
            output = collision_wireframe(package).to_json();
        else if (command == "collision")
            output = package.collision_snapshot();
        else
            output = (command == "textconv" || flags.contains("--snapshot")) ? package.semantic_snapshot()
                                                                             : package.to_json();
        if (package.source_read_limit_failed)
            output["sourceReadLimitFailed"] = true;
        if (!package.complete_input)
            exit_code = 2;
    } else if (image_command) {
        auto before = read(positional[0], "--before-project", options);
        if (!before.complete_input)
            throw Error("incomplete", "Required input payload is missing");
        auto mip = integer_option("--mip", 31);
        const auto &source = select_texture(before);
        if (command == "hdr-diff") {
            auto a = decode_texture_hdr(source, mip, texture_options);
            auto after = read(positional[1], "--after-project", options);
            if (!after.complete_input)
                throw Error("incomplete", "Required input payload is missing");
            auto b = decode_texture_hdr(select_texture(after), mip, after_texture_options);
            png_destination = path_option("--heatmap");
            auto comparison =
                compare_hdr_images(a, b, real_option("--threshold", 0, 0, std::numeric_limits<double>::max()),
                                   !png_destination.empty());
            output = comparison.to_json();
            output["mip"] = mip;
            exit_code = comparison.comparable && !comparison.changed_pixels ? 0 : 1;
            png = std::move(comparison.heatmap);
        } else if (command == "preview") {
            bool hdr = is_hdr_format(source.format);
            auto exposure = real_option("--exposure", 0, -32, 32);
            if (flags.contains("--exposure") && !hdr)
                throw Error("arguments", "--exposure requires an HDR source");
            auto image = hdr ? tone_map_hdr(decode_texture_hdr(source, mip, texture_options), exposure)
                             : decode_texture(source, mip, texture_options);
            png_destination = path_option("--out");
            if (png_destination.empty())
                throw Error("arguments", "preview requires --out IMAGE.png");
            output = {{"schema", "cyuediff.preview/1"},
                      {"width", image.width},
                      {"height", image.height},
                      {"mip", mip},
                      {"path", png_destination.u8string()},
                      {"sourceFormat", source.format},
                      {"conversion", hdr ? "HDR display: exposure, positive Reinhard tone mapping, "
                                           "linear-to-sRGB RGB; clamped alpha"
                                         : "RGBA8 encoded samples; no color-space conversion"}};
            if (hdr)
                output["exposureEV"] = exposure;
            png = std::move(image);
        } else {
            auto after = read(positional[1], "--after-project", options);
            if (!after.complete_input)
                throw Error("incomplete", "Required input payload is missing");
            const auto &other_source = select_texture(after);
            if (is_hdr_format(source.format) || is_hdr_format(other_source.format))
                throw Error("hdr-comparison",
                            "Use hdr-diff to compare HDR source samples before display conversion");
            auto image = decode_texture(source, mip, texture_options),
                 other = decode_texture(other_source, mip, after_texture_options);
            png_destination = path_option("--heatmap");
            auto comparison =
                compare_images(image, other, static_cast<std::uint8_t>(integer_option("--threshold", 255)),
                               !png_destination.empty());
            output = comparison.to_json();
            output["mip"] = mip;
            exit_code = comparison.comparable && !comparison.changed_pixels ? 0 : 1;
            png = std::move(comparison.heatmap);
        }
    } else if (geometry_command) {
        auto before = read(positional[0], "--before-project", options);
        if (!before.complete_input)
            throw Error("incomplete", "Required input payload is missing");
        auto lod = integer_option("--lod", 1000000);
        auto object = flags.value("--object", std::string{});
        auto geometry = decode_mesh(before.mesh_source(object, lod), mesh_options);
        if (command == "geometry")
            output = geometry.to_json();
        else if (command == "export-mesh") {
            obj_destination = path_option("--out");
            if (obj_destination.empty())
                throw Error("arguments", "export-mesh requires --out MESH.obj");
            output = {{"schema", "cyuediff.obj-export/1"},
                      {"vertices", geometry.vertices.size()},
                      {"instances", geometry.instances.size()},
                      {"triangles", geometry.triangles.size()},
                      {"lod", lod},
                      {"path", obj_destination.u8string()}};
            obj = std::move(geometry);
        } else {
            auto after = read(positional[1], "--after-project", options);
            if (!after.complete_input)
                throw Error("incomplete", "Required input payload is missing");
            auto other = decode_mesh(after.mesh_source(object, lod), after_mesh_options);
            auto comparison = compare_meshes(geometry, other);
            output = comparison.to_json();
            output["lod"] = lod;
            exit_code = comparison.changes.empty() ? 0 : 1;
        }
    } else if (command == "skeleton-pose-diff" || command == "socket-pose-diff") {
        if (flags.contains("--metadata") || flags.contains("--snapshot"))
            throw Error("arguments", "Reference pose comparison requires decoded hierarchy data");
        ReferencePoseDiffOptions pose_options;
        pose_options.pose.retarget_source=flags.value("--before-retarget-source",std::string{});
        if(flags.contains("--after-retarget-source"))pose_options.after_retarget_source=flags.at("--after-retarget-source").get<std::string>();
        pose_options.position_tolerance = real_option("--position-tolerance", 0, 0, std::numeric_limits<double>::max());
        const auto before = read(positional[0], "--before-project", options);
        const auto after = read(positional[1], "--after-project", options);
        if(command=="socket-pose-diff"){
            const auto comparison=compare_socket_poses(before,after,flags.value("--before-object",std::string{}),
                                                       flags.value("--after-object",std::string{}),pose_options);
            output=comparison.to_json();exit_code=comparison.status()=="identical"?0:comparison.status()=="changed"?1:2;
        }else{
        const auto comparison = compare_reference_poses(before, after, flags.value("--before-object",std::string{}),
                                                        flags.value("--after-object",std::string{}),pose_options);
        output = comparison.to_json();
        exit_code = comparison.status()=="identical" ? 0 : comparison.status()=="changed" ? 1 : 2;
        }
    } else if (command == "attachments-diff") {
        if(flags.contains("--metadata") || flags.contains("--snapshot"))
            throw Error("arguments","Attachment comparison requires decoded stored properties");
        const auto before=read(positional[0],"--before-project",options);
        const auto after=read(positional[1],"--after-project",options);
        const auto comparison=compare_attachments(before,after);
        output=comparison.to_json();exit_code=!comparison.complete?2:comparison.changes.empty()?0:1;
    } else if (command == "skeleton-animation-diff") {
        if (flags.contains("--metadata") || flags.contains("--snapshot"))
            throw Error("arguments","Skeleton animation metadata comparison requires native decoding");
        const auto before=read(positional[0],"--before-project",options);
        const auto after=read(positional[1],"--after-project",options);
        const auto comparison=compare_skeleton_animation(before,after);
        output=comparison.to_json();
        exit_code=!comparison.complete?2:comparison.changes.empty()?0:1;
    } else if (command == "skeleton-diff") {
        if (flags.contains("--metadata"))
            throw Error("arguments", "Skeleton comparison requires properties");
        const auto before = read(positional[0], "--before-project", options),
                   after = read(positional[1], "--after-project", options);
        const auto comparison = compare_skeletons(before, after);
        output = comparison.to_json();
        exit_code = !comparison.complete ? 2 : comparison.changes.empty() ? 0 : 1;
    } else if (command == "collision-diff") {
        if (flags.contains("--metadata"))
            throw Error("arguments", "Collision comparison requires properties");
        const auto before = read(positional[0], "--before-project", options);
        const auto after = read(positional[1], "--after-project", options);
        const auto comparison = compare_collisions(before, after);
        output = comparison.to_json();
        exit_code = !comparison.complete ? 2 : comparison.changes.empty() ? 0 : 1;
    } else if (command == "material-diff") {
        if (flags.contains("--metadata"))
            throw Error("arguments", "Material comparison requires properties");
        auto before = read(positional[0], "--before-project", options);
        auto after = read(positional[1], "--after-project", options);
        auto comparison = compare_materials(before, after);
        output = comparison.to_json();
        exit_code = !comparison.complete ? 2 : (comparison.changes.empty() ? 0 : 1);
    } else if (command == "diff") {
        if (flags.contains("--metadata"))
            throw Error("arguments", "Diff requires fingerprints and property reading");
        auto before = read(positional[0], "--before-project", options),
             after = read(positional[1], "--after-project", options);
        streamed_diff = compare(before, after);
        output["beforeDiagnostics"] = before.diagnostics;
        output["afterDiagnostics"] = after.diagnostics;
        exit_code =
            streamed_diff->status() == "identical"
                ? 0
                : ((streamed_diff->status() == "incomplete" || streamed_diff->status() == "unverified") ? 2
                                                                                                        : 1);
    } else if (command == "catalog") {
        auto context = ProjectContext::open(fs::u8path(positional[0]), root);
        auto catalog = catalog_assets(context, catalog_options());
        protected_files.push_back(context.project_file);
        for (const auto &mount : context.mounts)
            protected_directories.push_back(mount.directory);
        protected_directories.push_back(context.project_file.parent_path() / "Plugins");
        if (!context.engine.root.empty())
            protected_directories.push_back(context.engine.root / "Engine");
        for (const auto &asset : catalog.assets) {
            auto path = fs::u8path(asset.path);
            protected_files.push_back(path);
            for (const auto *extension : {".uexp", ".ubulk", ".uptnl", ".m.ubulk", ".o.ubulk", ".upayload"}) {
                auto companion = path;
                companion.replace_extension(extension);
                protected_files.push_back(companion);
            }
        }
        output = catalog.to_json();
        if (!catalog.complete)
            exit_code = 2;
    } else if (command == "project" || command == "resolve") {
        auto context = ProjectContext::open(fs::u8path(positional[0]), root);
        protected_files.push_back(context.project_file);
        output = command == "project" ? context.to_json()
                                      : Json{{"path", context.resolve(positional[1]).u8string()}};
    } else
        throw Error("arguments", "Unknown command " + command);
    if (source_read && (!before_limits.is_default() || !after_limits.is_default()))
        output["readLimits"] = {{"before", before_limits.to_json()}, {"after", after_limits.to_json()}};
    if (!source_types && (before_layouts || after_layouts)) {
        output["layoutContexts"] = {{"beforeSha256", before_layouts ? before_layouts->sha256() : std::string{}},
                                    {"afterSha256", after_layouts ? after_layouts->sha256() : std::string{}},
                                    {"contextChanged", before_layouts != after_layouts &&
                                        (!before_layouts || !after_layouts || before_layouts->sha256() != after_layouts->sha256())}};
    }

    auto destination = (command == "preview" || command == "zen-preview" || command == "export-mesh" ||
                        command == "zen-export-mesh" || command == "zen-export-physical-cloth" ||
                        command == "export-physical-cloth" || command == "container-read")
                           ? fs::path{}
                           : path_option("--out");
    auto guarded_path = [](const fs::path &path) {
        auto existing = fs::absolute(path).lexically_normal();
        fs::path suffix;
        // MSVC's weakly_canonical can fail fast for a missing child under an 8.3 parent.
        // Canonicalize the nearest existing ancestor, then append the missing components.
        while (!fs::exists(existing)) {
            const auto parent = existing.parent_path();
            if (parent == existing || parent.empty())
                throw Error("output-path", "Cannot resolve output ancestor");
            suffix = suffix.empty() ? existing.filename() : existing.filename() / suffix;
            existing = parent;
        }
        const auto resolved = fs::canonical(existing);
        return suffix.empty() ? resolved : resolved / suffix;
    };
    auto same_file = [&](const fs::path &a, const fs::path &b) {
        return guarded_path(a) == guarded_path(b) || (fs::exists(a) && fs::exists(b) && fs::equivalent(a, b));
    };
    for (const auto &target : {destination, png_destination, obj_destination, raw_destination}) {
        if (target.empty())
            continue;
        for (const auto &directory : protected_directories) {
            auto base = guarded_path(directory);
            auto resolved = guarded_path(target);
            auto relative = resolved.lexically_relative(base);
            bool within = resolved == base || (!relative.empty() && *relative.begin() != "..");
            for (auto parent = resolved; !within && !parent.empty();) {
                if (fs::exists(parent) && fs::exists(base) && fs::equivalent(parent, base))
                    within = true;
                auto next = parent.parent_path();
                if (next == parent)
                    break;
                parent = next;
            }
            if (within)
                throw Error("output-path",
                            "Output must not overwrite protected content, Git metadata or LFS storage");
        }
        for (const auto &input : protected_files)
            if (same_file(input, target))
                throw Error("output-path",
                            "Output must not overwrite an input package, companion or project descriptor");
    }
    if (!destination.empty() && !png_destination.empty() && same_file(destination, png_destination))
        throw Error("output-path", "Report and image must have distinct destinations");
    if (!raw_destination.empty()) {
        std::ofstream stream(raw_destination, std::ios::binary);
        if (!stream)
            throw Error("io", "Cannot create member output");
        stream.write(reinterpret_cast<const char *>(raw_bytes.data()),
                     static_cast<std::streamsize>(raw_bytes.size()));
        if (!stream)
            throw Error("io", "Cannot write member output");
    }
    if (!obj_destination.empty())
        write_obj(obj_destination, obj);
    if (!png_destination.empty())
        write_png(png_destination, png);
    auto write_report = [&](std::ostream &stream) {
        if (streamed_diff)
            streamed_diff->write_json(stream, 2, output);
#ifdef CYUEDIFF_HAS_GIT
        else if (streamed_git) {
            auto report = detail::git_report(*streamed_git);
            for (auto i = output.begin(); i != output.end(); ++i)
                report.borrow(i.key(), i.value());
            report.write(stream);
        }
#endif
        else {
            detail::JsonOutput writer(stream, 2);
            writer.value(output);
        }
        stream.put('\n');
        if (!stream)
            throw Error("io", "Cannot write output file");
    };
    if (destination.empty())
        write_report(std::cout);
    else
        detail::atomic_output(destination, write_report);
    return exit_code;
}
// Error reports must not throw: MSVC filesystem/system_error messages are in the ANSI code
// page, so invalid UTF-8 is replaced instead of raising a second exception in the handler.
int report_error(const Json &error) noexcept {
    try {
        std::cerr << error.dump(-1, ' ', false, Json::error_handler_t::replace) << '\n';
    } catch (...) {
        std::cerr << "{\"error\":\"exception\"}\n";
    }
    return 2;
}
template <class Run> int checked(Run &&run) noexcept {
    try {
        return run();
    } catch (const Error &e) {
        return report_error({{"error", e.code}, {"message", e.what()}, {"offset", e.offset}});
    } catch (const std::exception &e) {
        return report_error({{"error", "exception"}, {"message", e.what()}});
    } catch (...) {
        return report_error({{"error", "exception"}});
    }
}
int checked_run(const std::vector<std::string> &args) {
    return checked([&] { return run(args); });
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    return checked([&] {
        std::vector<std::string> args;
        for (int i = 1; i < argc; ++i)
            args.push_back(fs::path(argv[i]).u8string());
        return run(args);
    });
}
#else
int main(int argc, char **argv) { return checked_run({argv + 1, argv + argc}); }
#endif
