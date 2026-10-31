#pragma once

#include "Animation/Transform.h"
#include "Animation/BoneMask.h"
#include "Util/General.h"
#include <span>
#include <ozz/base/maths/soa_transform.h>

namespace Animation
{
    // Forward declaration interfejsu
    class IAnimEventHandler;

    class IAnimEventHandler
    {
    public:
        virtual ~IAnimEventHandler() = default;
        virtual void QueueEvent(const RE::BSFixedString& a_event, const RE::BSFixedString& a_arg) { (void)a_event; (void)a_arg; }
    };

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
        }

        virtual void StopAnimation() {
        }

        // Set bone mask for partial-body animation (nullptr = full body)
        void SetBoneMask(const BoneMask* a_mask) { _boneMask = a_mask; }
        const BoneMask* GetBoneMask() const { return _boneMask; }

        // Enhanced blend parameters
        struct BlendParams {
            float weight = 1.0f;
            const BoneMask* mask = nullptr;
            bool additive = false;  // future: additive blending
        };
        
        void SetBlendParams(const BlendParams& a_params) { 
            _blendParams = a_params; 
            if (a_params.mask) {
                _boneMask = a_params.mask;
            }
        }
        const BlendParams& GetBlendParams() const { return _blendParams; }

    protected:
        const BoneMask* _boneMask = nullptr;
        BlendParams _blendParams;
    };
}
