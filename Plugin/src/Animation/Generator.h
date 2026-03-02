#pragma once

#include "Animation/Transform.h"
#include "Util/General.h"
#include <span>
#include <ozz/base/maths/soa_transform.h>

namespace Animation
{
    // Forward declaration interfejsu
    class IAnimEventHandler;

    class Generator
    {
    public:
        virtual ~Generator() = default;

        struct Params {
            float fWeight = 1.0f;
            float fSpeed = 1.0f;
            bool bLooping = false;
        };

        // Główna metoda generowania pozy, którą musi zaimplementować każdy generator
        virtual std::span<ozz::math::SoaTransform> Generate(IAnimEventHandler* a_eventHandler) = 0;

        virtual void PlayAnimation(const std::string& a_animName, const Params& a_params) {
            SAF_LOG_DEBUG("Playing animation: {}", a_animName);
        }

        virtual void StopAnimation() {
            SAF_LOG_DEBUG("Stopping animation");
        }
    };
}