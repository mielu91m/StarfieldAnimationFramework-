#pragma once

#include "IPostGenJob.h"
#include "PCH.h"
#include <ozz/base/maths/simd_math.h>

namespace Animation
{
	class SpringBoneJob : public IPostGenJob
	{
	public:
		ozz::math::SimdFloat4 prevRootPos;
		ozz::math::SimdFloat4 gravity;
		float stiffness = 0.0f;
		float damping = 0.0f;
		float mass = 0.0f;
		uint32_t springId = 0;
		uint16_t boneIdx = 0;
		uint16_t parentIdx = 0;

		bool Run(const Context& a_context) override;
		GUID GetGUID() override;
		void Destroy() override;
	};
}
