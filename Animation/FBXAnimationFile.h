#pragma once

#include "IAnimationFile.h"
#include "IBasicAnimation.h"
#include "Ozz.h"
#include <ozz/animation/offline/fbx/fbx.h>
#include <ozz/animation/offline/fbx/fbx_skeleton.h>
#include <ozz/animation/offline/fbx/fbx_animation.h>
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/animation/runtime/animation.h>
#include <ozz/base/memory/unique_ptr.h>
#include <memory>
#include <string>
#include <vector>

namespace RE
{
    class Actor;
}

namespace Animation
{
    // Live actor rest pose, kept for API compatibility (GraphManager captures
    // its own). The GLB-shaped FBX path does not need it.
    struct GameRestPose
    {
        std::vector<ozz::math::Quaternion> rotations;
        std::vector<int16_t> parents;
        std::vector<ozz::math::Float3> translations;

        bool Valid(size_t a_jointCount) const
        {
            return rotations.size() == a_jointCount &&
                parents.size() == a_jointCount &&
                translations.size() == a_jointCount;
        }
    };

    class FBXAnimationFile : public IBasicAnimation
    {
    public:
        FBXAnimationFile(const std::string& a_filePath, std::shared_ptr<OzzSkeleton> a_gameSkeleton);

        /// World-formula loader: absolute game locals computed against the
        /// race skeleton NIF bind pose (static; live actor capture only as
        /// fallback). Feeds the identical GraphManager (GLB) writer path
        /// (NAF full writes).
        /// Returns nullptr on failure (e.g. rest capture failed).
        static std::unique_ptr<RawOzzAnimation> BuildWorldClip(
            const std::string& a_filePath, std::shared_ptr<OzzSkeleton> a_gameSkeleton, RE::Actor* a_actor);

        /// Y-up to Z-up conversion mode for the file side.
        /// 0 = Rx(+90), 1 = Rx(-90), 2 = identity (no conversion).
        static int GetConvertMode();

        /// Follow actor with dance travel (pelvis delta -> data.location).
        static bool GetFollowActor();

        /// Snap travel target onto the nearest walkable navmesh triangle
        /// (collision: walls/furniture block, Z glued to the floor).
        static bool GetTravelNavmeshSnap();

        /// Retarget rest mode: false = per-bone rest realign (delta form),
        /// true = absolute pose copy (bone rest world := game bind world).
        static bool GetRestAbs();
        static void SetRestAbs(bool a_abs);

        /// Last built root-travel keys by file basename (for actor follow).
        static bool GetRootTravel(const std::string& a_baseName,
            std::vector<RawOzzAnimation::RootMotionKey>& a_keysOut, float& a_durOut);

        /// Live limb trims (thighL/thighR/clavL/clavR/feet Euler XYZ, no restart).
        /// part: 0=thighL, 1=thighR, 2=clavL, 3=clavR, 4=feet.
        static void SetLimbTrim(int a_part, float a_x, float a_y, float a_z);
        static void GetLimbTrims(float a_out[9][3]);

        std::unique_ptr<Generator> CreateGenerator() override;
        size_t GetSizeBytes() override;
        void SampleBoneAnimation(float a_time, const ozz::span<ozz::math::SoaTransform>& a_transformsOut, IBasicAnimationContext* a_context) override;
        void SampleFaceAnimation(float a_time, const std::span<float>& a_morphsOut) override;
        void SampleEventTrack(float a_time, float a_timeStep, IAnimEventHandler* a_eventHandler) override;
        bool HasBoneAnimation() override { return hasBoneAnimation; }
        bool HasFaceAnimation() override { return hasFaceAnimation; }
        float GetDuration() override;
        std::unique_ptr<IBasicAnimationContext> CreateContext() override;
        const std::vector<uint8_t>& GetJointHasChannel() const { return jointHasChannel; }

    private:
        static void CollectJointNames(const ozz::animation::offline::RawSkeleton::Joint& joint, std::vector<std::string>& out);

        ozz::unique_ptr<ozz::animation::offline::RawAnimation> raw_animation;
        std::shared_ptr<OzzSkeleton> skeleton;
        std::unique_ptr<RawOzzFaceAnimation> face_animation_data;
        std::vector<uint8_t> jointHasChannel;

        bool hasBoneAnimation = false;
        bool hasFaceAnimation = false;
        float duration = 0.0f;
    };
}
