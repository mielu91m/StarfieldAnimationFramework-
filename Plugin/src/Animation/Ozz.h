#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "ozz/animation/offline/raw_animation.h"
#include "ozz/animation/offline/raw_track.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/base/maths/transform.h"
#include "ozz/base/memory/unique_ptr.h"

namespace Animation
{
    // Używamy forward declaration, jeśli klasa jest zdefiniowana w Graph.h
    class IAnimEventHandler;

    class OzzSkeleton
    {
    public:
        ozz::unique_ptr<ozz::animation::Skeleton> data;
        std::string name;
        std::vector<std::string> jointNames;
        std::vector<std::vector<std::string>> jointAliases;
        std::vector<bool> controlledByGame;

        const ozz::animation::Skeleton* GetRawSkeleton() const {
            return data.get();
        }
    };

    struct RawOzzFaceAnimation
    {
        std::array<ozz::animation::offline::RawFloatTrack, 104> tracks;
        float duration;
    };

    struct RawOzzAnimation
    {
        ozz::unique_ptr<ozz::animation::offline::RawAnimation> data = nullptr;
        std::unique_ptr<RawOzzFaceAnimation> faceData = nullptr;
        // For each joint track (same indexing as jointNames / RawAnimation::tracks):
        // 1 if the GLTF clip had at least one TRS channel for that joint; 0 otherwise.
        // Used to avoid applying default/identity transforms to sensitive joints (e.g. face bones) when the clip doesn't animate them.
        std::vector<std::uint8_t> jointHasChannel;

        // Bind (rest) pose the clip's local transforms are relative to, indexed
        // like tracks/jointNames. For GLB: node TRS transforms from the file.
        // For FBX world clips: game local rest (gameRestRotations/Translations).
        // Used as 'rest' in the delta computation (delta = inv(bind) * anim).
        // MUST NOT be replaced by clip frame 0: multi-actor / furniture clips
        // start in a scene pose (lying/sitting/intertwined), so frame 0 as rest
        // collapses the absolute pose and actors hover next to / above furniture.
        std::vector<ozz::math::Transform> bindPose;

        // True when clip local transforms are authored in GLTF Y-up space and
        // must be converted to game Z-up on write. GLB files (Blender/glTF
        // standard) need it; FBX world clips are already built in game Z-up
        // space and must NOT be converted again. ANDed with the global
        // ApplyYUpToZUpConversion INI switch at apply time.
        bool needsYUpToZUpConversion = true;

        // Root motion from the FBX skeleton root: world-space translation (already converted
        // to Z-up game space) over time. Used by FBXGraphManager to move the actor during playback.
        struct RootMotionKey
        {
            float time;
            ozz::math::Float3 translation;
        };
        std::vector<RootMotionKey> rootMotion;
    };
}
