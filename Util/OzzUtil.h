#pragma once

#include "Util/General.h"
#include "Util/String.h"
#include "Animation/Transform.h"
#include <ozz/base/maths/soa_transform.h>
#include <array>
#include <optional>
#include <string_view>
#include <ozz/base/maths/soa_float.h>
#include <ozz/base/maths/soa_quaternion.h>
#include <ozz/base/maths/simd_quaternion.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/animation/runtime/skeleton.h>
#include <span>
#include <vector>

namespace Util::Ozz
{
	/// Wyciąga z macierzy 4x4 znormalizowaną kwaternion rotacji (do użycia w Physics/Body itd.).
	inline ozz::math::SimdQuaternion ToNormalizedQuaternion(const ozz::math::Float4x4& a_transform)
	{
		ozz::math::SimdFloat4 t, s;
		ozz::math::SimdQuaternion r;
		ozz::math::ToAffine(a_transform, &t, &r.xyzw, &s);
		return r;
	}

	inline void ApplySoATransformTranslation(int32_t a_index, const ozz::math::SimdFloat4& a_trans, const std::span<ozz::math::SoaTransform>& a_transforms)
	{
		ozz::math::SoaTransform& soa_transform_ref = a_transforms[a_index / 4];
		ozz::math::SimdFloat4 aos_trans[4];
		ozz::math::Transpose4x4(&soa_transform_ref.translation.x, aos_trans);

		aos_trans[a_index & 3] = a_trans;

		ozz::math::Transpose4x4(aos_trans, &soa_transform_ref.translation.x);
	}

	inline ozz::math::SimdQuaternion GetSoATransformQuaternion(int32_t a_index, const std::span<ozz::math::SoaTransform>& a_transforms)
	{
		ozz::math::SoaTransform& soa_transform_ref = a_transforms[a_index / 4];
		ozz::math::SimdQuaternion aos_quats[4];
		ozz::math::Transpose4x4(&soa_transform_ref.rotation.x, &aos_quats->xyzw);
		return aos_quats[a_index & 3];
	}

	inline ozz::math::SimdFloat4 GetSoATransformTranslation(int32_t a_index, const std::span<ozz::math::SoaTransform>& a_transforms)
	{
		ozz::math::SoaTransform& soa_transform_ref = a_transforms[a_index / 4];
		ozz::math::SimdFloat4 aos_trans[4];
		ozz::math::Transpose4x4(&soa_transform_ref.translation.x, aos_trans);
		return aos_trans[a_index & 3];
	}

	inline void ApplySoATransformQuaternion(int32_t a_index, const ozz::math::SimdQuaternion& a_quat, const std::span<ozz::math::SoaTransform>& a_transforms)
	{
		ozz::math::SoaTransform& ref = a_transforms[a_index / 4];
		ozz::math::SimdQuaternion aos[4];
		ozz::math::Transpose4x4(&ref.rotation.x, &aos->xyzw);
		aos[a_index & 3] = a_quat;
		ozz::math::Transpose4x4(&aos->xyzw, &ref.rotation.x);
	}

	inline void MultiplySoATransformQuaternion(int32_t a_index, const ozz::math::SimdQuaternion& a_quat, const std::span<ozz::math::SoaTransform>& a_transforms)
	{
		ozz::math::SoaTransform& ref = a_transforms[a_index / 4];
		ozz::math::SimdQuaternion aos[4];
		ozz::math::Transpose4x4(&ref.rotation.x, &aos->xyzw);
		aos[a_index & 3] = aos[a_index & 3] * a_quat;
		ozz::math::Transpose4x4(&aos->xyzw, &ref.rotation.x);
	}

	template <typename... Args>
	inline std::optional<std::array<uint16_t, sizeof...(Args)>> GetJointIndexes(const ozz::animation::Skeleton* a_skeleton, Args... a_targetNames)
	{
		constexpr size_t numArgs = sizeof...(Args);
		std::array<std::string_view, numArgs> strings = { a_targetNames... };
		std::array<uint16_t, sizeof...(Args)> result;

		for (size_t i = 0; i < numArgs; i++) {
			result[i] = std::numeric_limits<uint16_t>::max();
		}

		const auto jointNames = a_skeleton->joint_names();
		for (auto iter = jointNames.begin(); iter != jointNames.end(); iter++) {
			for (size_t i = 0; i < numArgs; i++) {
				if (Util::String::CaseInsensitiveCompare(strings[i], *iter)) {
					result[i] = static_cast<uint16_t>(std::distance(jointNames.begin(), iter));
				}
			}
		}

		for (size_t i = 0; i < numArgs; i++) {
			if (result[i] == std::numeric_limits<uint16_t>::max()) {
				return std::nullopt;
			}
		}
		return result;
	}

	inline ozz::math::Float4x4 GetParentTransform(uint16_t a_targetNode, const std::span<ozz::math::Float4x4>& a_modelSpace, const ozz::animation::Skeleton* a_skeleton)
	{
		int16_t parent = a_skeleton->joint_parents()[a_targetNode];
		if (parent == ozz::animation::Skeleton::kNoParent)
			return ozz::math::Float4x4::identity();
		return a_modelSpace[parent];
	}

	inline void ApplyMSRotationToLocal(const ozz::math::SimdQuaternion& a_rotation, uint16_t a_targetNode,
		const std::span<ozz::math::SoaTransform>& a_localSpace, const std::span<ozz::math::Float4x4>& a_modelSpace,
		const ozz::animation::Skeleton* a_skeleton)
	{
		ozz::math::Float4x4 parentTransform = GetParentTransform(a_targetNode, a_modelSpace, a_skeleton);
		ozz::math::SimdQuaternion parentRotation = ToNormalizedQuaternion(parentTransform);
		ApplySoATransformQuaternion(static_cast<int32_t>(a_targetNode), ozz::math::Conjugate(parentRotation) * a_rotation, a_localSpace);
	}

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
