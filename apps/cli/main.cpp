#include "binary_commands.hpp"
#include <cyuediff/curves.hpp>
#include <cyuediff/animation.hpp>
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
#include <cyuediff/cooked_physics.hpp>
#include <cyuediff/schema_context.hpp>
#include <cyuediff/binary_layout.hpp>
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
               "  cyuediff layouts DESCRIPTION.json [--out REPORT.json]\n"
               "  inspect/diff/git-inspect/git-diff accept --layouts DESCRIPTION.json.\n"
               "  diff/git-diff accept independent --after-layouts DESCRIPTION.json.\n"
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
               "  cyuediff zen-skeletal CONTAINER ENTRY --skeletal-profile ue5.3-windows --keep-mobile-min-lod 0|1\n"
               "    [--has-vertex-colors 0|1] [--export-index N] [same cooked schema context]\n"
               "  cyuediff zen-skeletal-diff BEFORE ENTRY AFTER ENTRY [same skeletal context]\n"
               "    [--after-has-vertex-colors 0|1] [--lod N] [--after-lod N]\n"
               "  cyuediff zen-mesh CONTAINER ENTRY --mesh-profile ue5.3-windows --keep-mobile-min-lod 0|1\n"
               "  cyuediff zen-geometry CONTAINER ENTRY [--lod N] [--export-index N]\n"
               "  cyuediff zen-export-mesh CONTAINER ENTRY --out MESH.obj [--lod N]\n"
               "  cyuediff zen-mesh-diff BEFORE ENTRY AFTER ENTRY [--lod N] [--after-lod N]\n"
               "  Mesh commands also require --cooked-profile ue5.3 --schema CONTEXT.json.\n"
               "  cyuediff zen-physics CONTAINER ENTRY --physics-profile ue5.3-windows [cooked context]\n"
               "  cyuediff zen-physics-wire CONTAINER ENTRY [same explicit cooked physics context]\n"
               "  cyuediff zen-physics-diff BEFORE ENTRY AFTER ENTRY [same cooked context]\n"
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
    if (is_binary_command(args[0]))
        return run_binary_command(args);
    std::vector<std::string> positional;
    Json flags = Json::object();
    const std::set<std::string> valued = {"--physics-profile",
                                          "--after-physics-profile",
                                          "--mesh-profile",
                                          "--after-mesh-profile",
                                          "--keep-mobile-min-lod",
                                          "--after-keep-mobile-min-lod",
                                          "--skeletal-profile",
                                          "--after-skeletal-profile",
                                          "--has-vertex-colors",
                                          "--after-has-vertex-colors",
                                          "--after-lod",
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
                   arg == "--include-engine")
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
    auto required = (command == "diff" || command == "resolve" || command == "image-diff" ||
                     source_pose_single || source_pose_diff || command == "animation-frame-diff" || command == "curve-value-diff" || command == "curves-diff" || command == "animation-diff" || command == "attachments-diff" || command == "socket-pose-diff" || command == "skeleton-animation-diff" || command == "skeleton-pose-diff" || command == "collision-diff" || command == "skeleton-diff" || command == "mesh-diff" ||
                     command == "material-diff" || command == "hdr-diff" || command == "container-read" ||
                     command == "container-diff")
                        ? 2u
                        : 1u;
    const bool git_command = command == "git-inspect" || command == "git-diff" || command == "git-project" ||
                             command == "git-catalog" || command == "git-history";
    const bool schema_command = command == "usmap";
    const bool cooked_physics =
        command == "zen-physics" || command == "zen-physics-wire" || command == "zen-physics-diff";
    const bool cooked_physics_diff = command == "zen-physics-diff";
    const bool cooked_collision =
        command == "zen-collision" || command == "zen-collision-wire" || command == "zen-collision-diff";
    const bool cooked_collision_diff = command == "zen-collision-diff";
    const bool cooked_skeletal = command == "zen-skeletal" || command == "zen-skeletal-diff";
    const bool cooked_skeletal_diff = command == "zen-skeletal-diff";
    const bool cooked_mesh = command == "zen-mesh" || command == "zen-geometry" ||
                             command == "zen-export-mesh" || command == "zen-mesh-diff";
    const bool cooked_mesh_diff = command == "zen-mesh-diff";
    const bool cooked_pixels = command == "zen-texture" || command == "zen-preview" || command == "zen-image-diff" || command == "zen-hdr-diff";
    const bool cooked_pixel_diff = command == "zen-image-diff" || command == "zen-hdr-diff";
    const bool cooked_compare = command == "zen-diff" || cooked_pixel_diff || cooked_mesh_diff ||
                                cooked_collision_diff || cooked_physics_diff || cooked_skeletal_diff;
    const bool zen_command = command == "zen-inspect" || command == "zen-diff" || command == "zen-catalog" ||
                             cooked_pixels || cooked_mesh || cooked_collision || cooked_physics || cooked_skeletal;
    if (zen_command)
        required = cooked_compare ? 4u : (command == "zen-catalog" ? 1u : 2u);
    const bool container_command = command == "container" || command == "container-read" ||
                                   command == "container-hash" || command == "container-diff";
    const bool catalog_command = command == "catalog" || command == "git-catalog";
    if (git_command)
        required =
            command == "git-diff" ? 4u : ((command == "git-catalog" || command == "git-history") ? 2u : 3u);
    if (positional.size() != required)
        throw Error("arguments", "Wrong number of arguments; use --help");
    const bool image_command = command == "preview" || command == "image-diff" || command == "hdr-diff";
    const bool geometry_command = command == "geometry" || command == "export-mesh" || command == "mesh-diff";
    for (auto it = flags.begin(); it != flags.end(); ++it) {
        const auto &key = it.key();
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
        if((key=="--time-start" || key=="--time-end" || key=="--samples" || key=="--before-lane" || key=="--after-lane" || key=="--value-tolerance") && command!="curve-value-diff")
            throw Error("arguments",key+" requires curve-value-diff");
        if(key=="--curve-family" && command!="curves" && command!="curves-diff" && command!="curve-value" && command!="curve-value-diff" && command!="curve-components")throw Error("arguments","--curve-family requires a curve command");
        if(key=="--curve-id" && command!="curve-components")throw Error("arguments","--curve-id requires curve-components");
        if(key=="--time" && command!="curve-value" && command!="curve-components")throw Error("arguments","--time requires curve-value or curve-components");
        if(key=="--lane" && command!="curve-value")throw Error("arguments","--lane requires curve-value");
        if(key=="--frame" && command!="animation-frame" && !source_pose_single)throw Error("arguments","--frame requires animation-frame");
        if((key=="--before-frame" || key=="--after-frame") && command!="animation-frame-diff" && !source_pose_diff)throw Error("arguments",key+" requires animation-frame-diff or animation-pose-diff");
        if((key=="--rotation-tolerance" || key=="--scale-tolerance") && command!="animation-frame-diff")throw Error("arguments",key+" requires animation-frame-diff");
        if(key=="--skeleton-object" && !source_pose_single)throw Error("arguments",key+" requires animation-pose");
        if((key=="--before-skeleton" || key=="--after-skeleton" || key=="--before-skeleton-object" || key=="--after-skeleton-object") && !source_pose_diff)throw Error("arguments",key+" requires animation-pose-diff");
        if ((key == "--before-object" || key == "--after-object") && command != "curve-value-diff" && command != "curves-diff" && command != "animation-diff" && command != "animation-frame-diff" && !source_pose_diff &&
            command != "skeleton-pose-diff" && command != "socket-pose-diff")
            throw Error("arguments", key + " requires a model or pose comparison");
        if(key=="--position-tolerance" && command!="skeleton-pose-diff" && command!="socket-pose-diff" && command!="animation-frame-diff" && !source_pose_diff)
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
            if (key != "--out" && key != "--codec" && key != "--cooked-profile" &&
                key != "--script-objects" && key != "--schema" &&
                !(key == "--snapshot" && (command == "zen-inspect" || command == "zen-catalog")) &&
                !(command == "zen-catalog" && (key == "--max-packages" || key == "--max-exports")) &&
                !((cooked_pixels || cooked_mesh || cooked_skeletal) && key == "--export-index") &&
                !(cooked_physics && key == "--physics-profile") &&
                !(cooked_physics_diff && key == "--after-physics-profile") &&
                !(cooked_skeletal && (key == "--skeletal-profile" || key == "--has-vertex-colors" || key == "--keep-mobile-min-lod")) &&
                !(cooked_skeletal_diff && (key == "--after-skeletal-profile" || key == "--after-has-vertex-colors" || key == "--after-keep-mobile-min-lod" || key == "--after-export-index" || key == "--lod" || key == "--after-lod")) &&
                !(cooked_mesh && (key == "--mesh-profile" || key == "--keep-mobile-min-lod")) &&
                !(cooked_mesh && command != "zen-mesh" && key == "--lod") &&
                !(cooked_mesh_diff &&
                  (key == "--after-mesh-profile" || key == "--after-keep-mobile-min-lod" ||
                   key == "--after-lod" || key == "--after-export-index")) &&
                !(cooked_pixels && command != "zen-texture" && (key == "--mip" || key == "--platform")) &&
                !(command == "zen-preview" && key == "--exposure") &&
                !(cooked_pixel_diff && (key == "--threshold" || key == "--heatmap" ||
                                        key == "--after-platform" || key == "--after-export-index")) &&
                !(cooked_compare && (key == "--after-cooked-profile" || key == "--after-script-objects" ||
                                     key == "--after-schema")))
                throw Error("arguments", "Unsupported cooked option: " + key);
            continue;
        }
        if (key == "--physics-profile" || key == "--after-physics-profile")
            throw Error("arguments", key + " requires a cooked physics command");
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
    std::vector<std::uint8_t> raw_bytes;
    fs::path raw_destination;
    MeshGeometry obj;
    fs::path obj_destination;
    ImageRgba8 png;
    fs::path png_destination;
    int exit_code = 0;
    ReadOptions options;
    options.decoders = before_decoders;
    options.narrow_string_encoding = before_layouts ? before_layouts->narrow_string_encoding() : NarrowStringEncoding::unspecified;
    auto after_read_options = options;
    after_read_options.decoders = after_decoders;
    after_read_options.narrow_string_encoding = after_layouts ? after_layouts->narrow_string_encoding() : NarrowStringEncoding::unspecified;
    if (command == "layouts") {
        const auto input = fs::u8path(positional[0]);
        protected_files.push_back(input);
        const auto catalog = BinaryLayouts::read(input);
        output = {{"schema", "cyuediff.binary-layout-catalog/1"}, {"sha256", catalog.sha256()},
                  {"decoderCount", catalog.decoder_registry()->size()}, {"definition", catalog.to_json()}};
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
                exit_code = result.changes.empty() ? 0 : 1;
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
            opts.geometry = command != "zen-mesh";
            auto before = read_zen_static_mesh(input, select(input, positional[1]), before_options,
                                               read_options, opts, selected_export("--export-index"));
            auto geometry = [&](const CookedStaticMesh &mesh, std::size_t lod) -> const MeshGeometry & {
                if (lod >= mesh.lods.size() || !mesh.lods[lod].geometry)
                    throw Error("cooked-mesh-unavailable",
                                "Selected cooked mesh LOD geometry is unavailable");
                return *mesh.lods[lod].geometry;
            };
            const auto lod = integer_option("--lod", 1'000'000);
            if (command == "zen-mesh")
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
            output = comparison.to_json();
            auto status = comparison.status();
            exit_code =
                status == "identical" ? 0 : ((status == "incomplete" || status == "unverified") ? 2 : 1);
        }
#else
        throw Error("git-unavailable", "Configure CYUEDIFF_BUILD_GIT=ON to enable Git commands");
#endif
    } else if(command=="curve-value-diff" || command=="curves" || command=="curves-diff" || command=="curve-value" || command=="curve-components"){
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
    } else if(source_pose_single || source_pose_diff) {
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
    } else if(command=="animation-frame-diff") {
        if(flags.contains("--metadata") || flags.contains("--snapshot") || !flags.contains("--before-frame") || !flags.contains("--after-frame"))throw Error("arguments","animation-frame-diff requires decoded properties, --before-frame and --after-frame");
        AnimationFrameDiffOptions selected;
        selected.position_tolerance=real_option("--position-tolerance",0,0,std::numeric_limits<double>::max());
        selected.rotation_tolerance_degrees=real_option("--rotation-tolerance",0,0,180);
        selected.scale_tolerance=real_option("--scale-tolerance",0,0,std::numeric_limits<double>::max());
        const auto af=integer_option("--before-frame",INT32_MAX),bf=integer_option("--after-frame",INT32_MAX);
        const auto before=read(positional[0],"--before-project",options),after=read(positional[1],"--after-project",options);
        output=compare_animation_frames(before,after,af,bf,flags.value("--before-object",std::string{}),flags.value("--after-object",std::string{}),selected);
        exit_code=output["status"]=="identical"?0:output["status"]=="changed"?1:2;
    } else if(command=="animation" || command=="animation-frame" || command=="animation-diff") {
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
    } else if (command == "inspect" || command == "textconv" || command == "graph" || command == "texture" ||
               command == "mesh" || command == "material" || command == "collision" ||
               command == "collision-preview" || command == "skeleton" || command == "skeleton-animation" || command == "skeleton-pose" || command=="attachments" || command=="socket-pose") {
        if ((command == "graph" || command == "texture" || command == "mesh" || command == "material" ||
             command == "collision" || command == "collision-preview" || command == "skeleton" ||
             command == "skeleton-pose" || command == "skeleton-animation" || command=="attachments" || command=="socket-pose") &&
            flags.contains("--metadata"))
            throw Error("arguments", "Graph/texture inspection requires properties");
        if (flags.contains("--metadata"))
            options.properties = options.fingerprints = false;
        auto package = read(positional[0], "--project", options);
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
    } else if (command == "skeleton-pose-diff" || command=="socket-pose-diff") {
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
    } else if(command=="attachments-diff"){
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
        auto result = compare(before, after);
        output = result.to_json();
        output["beforeDiagnostics"] = before.diagnostics;
        output["afterDiagnostics"] = after.diagnostics;
        exit_code = result.status() == "identical"
                        ? 0
                        : ((result.status() == "incomplete" || result.status() == "unverified") ? 2 : 1);
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
    if (before_layouts || after_layouts) {
        output["layoutContexts"] = {{"beforeSha256", before_layouts ? before_layouts->sha256() : std::string{}},
                                    {"afterSha256", after_layouts ? after_layouts->sha256() : std::string{}},
                                    {"contextChanged", before_layouts != after_layouts &&
                                        (!before_layouts || !after_layouts || before_layouts->sha256() != after_layouts->sha256())}};
    }
    auto text = output.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
    auto destination = (command == "preview" || command == "zen-preview" || command == "export-mesh" ||
                        command == "zen-export-mesh" || command == "container-read")
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
    if (destination.empty())
        std::cout << text;
    else {
        std::ofstream stream(destination, std::ios::binary);
        if (!stream)
            throw Error("io", "Cannot create output file");
        stream << text;
        if (!stream)
            throw Error("io", "Cannot write output file");
    }
    return exit_code;
}
int checked_run(const std::vector<std::string> &args) {
    try {
        return run(args);
    } catch (const Error &e) {
        std::cerr << Json{{"error", e.code}, {"message", e.what()}, {"offset", e.offset}}.dump() << '\n';
        return 2;
    } catch (const std::exception &e) {
        std::cerr << Json{{"error", "exception"}, {"message", e.what()}}.dump() << '\n';
        return 2;
    }
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
        args.push_back(fs::path(argv[i]).u8string());
    return checked_run(args);
}
#else
int main(int argc, char **argv) { return checked_run({argv + 1, argv + argc}); }
#endif
