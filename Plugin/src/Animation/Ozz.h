#pragma once
#include <array>
#include <memory>
#include <string>
#include <vector>
#include "ozz/animation/offline/raw_animation.h"
#include "ozz/animation/offline/raw_track.h"
#include "ozz/animation/runtime/skeleton.h"
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
    };
}
