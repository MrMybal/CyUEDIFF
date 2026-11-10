#include <cyuediff/skeleton_pose.hpp>
#include <cyuediff/attachments_pose.hpp>
#include <iostream>
using namespace cyuediff;
int main(int argc, char **argv) {
    try {
        Package before, after;
        if (argc > 1) {
            before = Package::open(fs::u8path(argv[1]));
            after = argc > 2 ? Package::open(fs::u8path(argv[2])) : before;
        } else {
            // An explicit host-populated model demonstrates the public API without engine assets.
            ObjectExport e;
            e.path = e.name = "Skeleton";
            e.class_name = "/Script/Engine.Skeleton";
            e.coverage = "properties";
            e.native_coverage = "reference-skeleton";
            Json pose = {
                {"rotation", {0., 0., 0., 1.}}, {"translation", {0., 0., 0.}}, {"scale", {1., 1., 1.}}};
            auto arm = pose;
            arm["translation"] = {10., 0., 0.};
            e.native_data = {{"kind", "reference-skeleton"},
                             {"layout", "host-demo"},
                             {"bones", Json::array({{{"name", "root"}, {"parentIndex", -1}},
                                                    {{"name", "arm"}, {"parentIndex", 0}}})},
                             {"localPoses", Json::array({pose, arm})},
                             {"nameToIndex", {{"root", 0}, {"arm", 1}}}};
            e.native_data["curveMetadata"]={{"coverage","stored"},{"containers",
                {{"AnimationCurves",{{"Smile",{{"material",false},{"morphTarget",true},
                    {"linkedBones",{"arm"}},{"maxLOD",2}}}}}}}};
            e.native_data["syncMarkers"]={{"coverage","stored"},{"stripFlags",{{"global",0},{"class",0}}},
                {"names",{"Left","Right","Left"}}};
            auto property=[](const Json &v,const char *type,const char *parameter=nullptr){
                Json descriptor={{"name",type}};
                if(parameter)descriptor["parameters"]=Json::array({{{"name",parameter}}});
                return Json{{"type",descriptor},{"value",v}};
            };
            e.properties["Sockets"]=property({"Skeleton.Grip"},"ArrayProperty","ObjectProperty");
            e.properties["VirtualBones"]=property(Json::array(),"ArrayProperty","StructProperty");
            e.properties["VirtualBones"]["type"]["parameters"][0]["parameters"]=Json::array({{{"name","VirtualBone"}}});
            before.exports.push_back(e);
            ObjectExport socket;socket.name="Grip";socket.path="Skeleton.Grip";socket.outer_index=1;
            socket.class_name="/Script/Engine.SkeletalMeshSocket";socket.coverage="properties";
            socket.properties={{"SocketName",property("Grip","NameProperty")},{"BoneName",property("arm","NameProperty")},
                               {"RelativeLocation",property({1.,0.,0.},"StructProperty","Vector")}};
            before.exports.push_back(socket);
            // These original demo names are declared literal base names, with internal number zero.
            // A host reading actual files must use components provided by its decoder.
            before.name_values = {{"schema", "cyuediff.name-values/1"},
                                  {"root", "semantic-snapshot"},
                                  {"status", "complete"},
                                  {"names",
                                   {{"/objects/Skeleton.Grip/properties/SocketName/value",
                                     {{"base", "Grip"}, {"number", 0}, {"source", "host-demo-literal"}}},
                                    {"/objects/Skeleton.Grip/properties/BoneName/value",
                                     {{"base", "arm"}, {"number", 0}, {"source", "host-demo-literal"}}}}}};
            after = before;
            after.exports[0].native_data["localPoses"][1]["translation"][0] = 12.;
            after.exports[0].native_data["curveMetadata"]["containers"]["AnimationCurves"]["Smile"]["maxLOD"]=3;
            after.exports[0].native_data["syncMarkers"]["names"]={"Left","Left","Right"};
            after.exports[1].properties["RelativeLocation"]["value"][0]=2.;
        }
        Json result = {{"snapshot", before.skeleton_snapshot()},
                       {"comparison", compare_skeletons(before, after).to_json()},
                       {"pose", reference_pose(before).to_json()},
                       {"poseComparison", compare_reference_poses(before, after).to_json()}};
        result["attachmentSnapshot"]=attachment_snapshot(before);
        result["attachmentComparison"]=compare_attachments(before,after).to_json();
        result["socketPose"]=socket_pose(before).to_json();
        result["socketPoseComparison"]=compare_socket_poses(before,after).to_json();
        result["animationSnapshot"]=skeleton_animation_snapshot(before);
        result["animationComparison"]=compare_skeleton_animation(before,after).to_json();
        if (argc == 1) {
            auto missing_origin=before;missing_origin.exports[1].properties.erase("RelativeLocation");
            result["missingSocketOrigin"]=socket_pose(missing_origin).to_json();
            auto stripped=before;
            stripped.exports[0].native_data["syncMarkers"]={{"coverage","stripped"},
                {"stripFlags",{{"global",1},{"class",0}}}};
            result["strippedAnimationComparison"]=compare_skeleton_animation(stripped,stripped).to_json();
            // Explicit host data demonstrates named retarget selection without engine assets.
            auto retarget = before;
            auto local = retarget.exports[0].native_data["localPoses"];
            local[1]["translation"] = {15., 0., 0.};
            retarget.exports[0].native_data["retargeting"] = {
                {"coverage", "stored"},
                {"skeletonGuid", std::string(32, '0')},
                {"sources", {{"T_Pose", {{"poseName", "Display label"}, {"localPoses", local}}}}}};
            ReferencePoseOptions selected;
            selected.retarget_source = "T_Pose";
            result["retargetPose"] = reference_pose(retarget, "", selected).to_json();
            ReferencePoseDiffOptions options;
            options.pose = selected;
            options.after_retarget_source = std::string{};
            result["retargetAgainstRaw"] =
                compare_reference_poses(retarget, retarget, "", "", options).to_json();
            // A declared opaque interval without a fingerprint cannot establish equality.
            auto unverified = before;
            unverified.exports[0].opaque["suffix"] = {{"bytes", 4}};
            result["unverifiedNativeTail"] = compare_skeletons(unverified, unverified).to_json();
        }
        std::cout << result.dump(2) << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 2;
    }
}
