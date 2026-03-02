#pragma once

#include "Util/General.h"
#include "Animation/Transform.h"
#include <ozz/base/maths/soa_transform.h>
#include <ozz/base/maths/soa_float.h>
#include <ozz/base/maths/soa_quaternion.h>
#include <ozz/base/maths/simd_math.h>
#include <span>
#include <vector>

namespace Util::Ozz
{
    inline void PackSoaTransforms(std::span<const Animation::Transform> a_src, std::vector<ozz::math::SoaTransform>& a_dst)
    {
        // Ozz SOA stores 4 bones per SoaTransform; build manually (ozz has no Pack() in math namespace)
        size_t soa_count = (a_src.size() + 3) / 4;
        a_dst.resize(soa_count);
        using namespace ozz::math;

        for (size_t i = 0; i < soa_count; ++i) {
            ozz::math::Transform transforms[4];
            for (int j = 0; j < 4; ++j) {
                size_t idx = i * 4 + j;
                if (idx < a_src.size()) {
                    transforms[j].translation = Float3(a_src[idx].translation.x, a_src[idx].translation.y, a_src[idx].translation.z);
                    transforms[j].rotation = Quaternion(a_src[idx].rotation.x, a_src[idx].rotation.y, a_src[idx].rotation.z, a_src[idx].rotation.w);
                    // Animation::Transform uses uniform float scale, not Vector3
                    float s = a_src[idx].scale;
                    transforms[j].scale = Float3(s, s, s);
                } else {
                    transforms[j] = Transform::identity();
                }
            }
            SoaFloat3 trans = SoaFloat3::Load(
                simd_float4::Load(transforms[0].translation.x, transforms[1].translation.x, transforms[2].translation.x, transforms[3].translation.x),
                simd_float4::Load(transforms[0].translation.y, transforms[1].translation.y, transforms[2].translation.y, transforms[3].translation.y),
                simd_float4::Load(transforms[0].translation.z, transforms[1].translation.z, transforms[2].translation.z, transforms[3].translation.z));
            SoaQuaternion rot = SoaQuaternion::Load(
                simd_float4::Load(transforms[0].rotation.x, transforms[1].rotation.x, transforms[2].rotation.x, transforms[3].rotation.x),
                simd_float4::Load(transforms[0].rotation.y, transforms[1].rotation.y, transforms[2].rotation.y, transforms[3].rotation.y),
                simd_float4::Load(transforms[0].rotation.z, transforms[1].rotation.z, transforms[2].rotation.z, transforms[3].rotation.z),
                simd_float4::Load(transforms[0].rotation.w, transforms[1].rotation.w, transforms[2].rotation.w, transforms[3].rotation.w));
            SoaFloat3 scale = SoaFloat3::Load(
                simd_float4::Load(transforms[0].scale.x, transforms[1].scale.x, transforms[2].scale.x, transforms[3].scale.x),
                simd_float4::Load(transforms[0].scale.y, transforms[1].scale.y, transforms[2].scale.y, transforms[3].scale.y),
                simd_float4::Load(transforms[0].scale.z, transforms[1].scale.z, transforms[2].scale.z, transforms[3].scale.z));  // ozz::math::Float3 has x,y,z
            a_dst[i] = { trans, rot, scale };
        }
    }

    inline void Initialize() {
        SAF_LOG_INFO("Ozz Animation system initialized.");
    }
}
