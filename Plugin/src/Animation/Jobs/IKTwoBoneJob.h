#pragma once

#include "Animation/Easing.h"
#include "IPostGenJob.h"
#include "PCH.h"
#include <ozz/animation/runtime/ik_two_bone_job.h>

namespace Animation
{
	struct IKTwoBoneJob : public IPostGenJob
	{
		enum FLAG : uint8_t
		{
			kNoFlags = 0,
			kDeleteOnTransitionFinish = 1u << 0,
			kPendingDelete = 1u << 1,
			kKeepEndNodeMSRotation = 1u << 2
		};

		enum TransitionMode : uint8_t { kNone = 0, kIn = 1, kOut = 2 };

		struct TransitionData
		{
			float duration = 0.0f;
			float currentTime = 0.0f;
			TransitionMode mode = kNone;
			CubicInOutEase<float> ease;
		};

		RE::NiPoint3 target;
		RE::NiPoint3 poleDir;
		TransitionData transition;
		uint32_t chainId = 0;
		uint16_t start_node = 0;
		uint16_t mid_node = 0;
		uint16_t end_node = 0;
		uint8_t flags = kKeepEndNodeMSRotation;
		bool targetWithinRange = false;

		IKTwoBoneJob() = default;

		void TransitionIn(float a_duration);
		void TransitionOut(float a_duration, bool a_delete);
		void CalculateJobWeight(float a_deltaTime, ozz::animation::IKTwoBoneJob& a_job);
		bool Validate(const ozz::animation::Skeleton* a_skeleton);
		bool Update(float a_deltaTime, const std::span<ozz::math::SoaTransform>& a_localOutput,
			const std::span<ozz::math::Float4x4>& a_modelSpace, const ozz::math::Float4x4& a_invertedRoot,
			const ozz::animation::Skeleton* a_skeleton);

		bool Run(const Context& a_context) override;
		GUID GetGUID() override;
		void Destroy() override;
	};

	inline static bool ik2b_any(IKTwoBoneJob::FLAG f, uint8_t flags) { return (flags & f) != 0; }
	inline static bool ik2b_none(IKTwoBoneJob::FLAG f, uint8_t flags) { return (flags & f) == 0; }
}
