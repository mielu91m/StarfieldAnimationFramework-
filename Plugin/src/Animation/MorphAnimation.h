#pragma once

#include "PCH.h"
#include <ozz/base/maths/simd_math.h>
#include <memory>
#include <span>

namespace Animation
{
	class MorphAnimation;

	template <typename T>
	struct AllocatedArray
	{
		std::unique_ptr<T[]> data;
		size_t size{ 0 };

		void allocate(size_t a_size)
		{
			data = std::make_unique<T[]>(a_size);
			size = a_size;
		}

		std::span<T> as_span() const { return std::span<T>(data.get(), size); }
	};

	struct MorphContext
	{
		struct InterpSimdFloat
		{
			ozz::math::SimdFloat4 first;
			ozz::math::SimdFloat4 second;
		};

		float prevRatio = -1.0f;
		AllocatedArray<InterpSimdFloat> cachedKeys;
		AllocatedArray<uint16_t> entries;
		const MorphAnimation* animation = nullptr;

		float Step(const MorphAnimation* a_anim, float a_ratio);
		void Invalidate();
	};

	struct MorphTrack
	{
		AllocatedArray<uint16_t> keys;
		AllocatedArray<uint16_t> timeIdxs;
	};

	class MorphAnimation
	{
	public:
		void Sample(float a_time, MorphContext& a_context, const std::span<ozz::math::SimdFloat4>& a_output);

	private:
		AllocatedArray<float> timepoints;
		float duration = 0.0f;
	};
}
