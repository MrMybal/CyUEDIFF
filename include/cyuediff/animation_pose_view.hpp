#pragma once
#include <cyuediff/animation_pose.hpp>
#include <cyuediff/animation_socket_pose.hpp>
#include <cyuediff/reference_pose_viewport.hpp>
#include <cyui/modules/Diff.h>
namespace cyuediff {
// The host evaluates Core poses outside its render loop. This view owns bounded copies
// of those results; draw() never opens assets or evaluates animation/hierarchy data.
class AnimationPoseView {
    std::optional<AnimationPose> before_, after_;
    ReferencePoseViewport viewport_;
    cyui::TextFilter filter_;
    std::vector<std::string> names_;
    std::string selected_;
    Json comparison_ = Json::object();
    double position_tolerance_ = 0;
    bool comparing_ = false, truncated_ = false, socket_mode_ = false;
    void rebuild(bool preserve_camera);

  public:
    static void register_translations();
    void set_poses(const AnimationPose *before, const AnimationPose *after = nullptr, bool comparing = false);
    void set_socket_poses(const AnimationSocketPose *before, const AnimationSocketPose *after = nullptr,
                          bool comparing = false);
    bool select_socket(const std::string &name) { return socket_mode_ && select_joint(name); }
    std::size_t socket_count() const { return socket_mode_ ? names_.size() : 0; }
    const Json *display_difference(const std::string &name) const { return viewport_.joint_difference(name); }
    bool select_joint(const std::string &);
    void set_filter(std::string query) { filter_.query = std::move(query); }
    void set_position_tolerance(double);
    const Json &comparison() const { return comparison_; }
    std::size_t joint_count() const { return names_.size(); }
    std::size_t rendered_joints() const { return viewport_.rendered_joints(); }
    Json view_state() const;
    void restore_view_state(const Json &);
    void draw(const char *id = "Animation hierarchy pose");
};
} // namespace cyuediff
