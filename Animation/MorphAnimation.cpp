#include "MorphAnimation.h"

namespace Animation
{
	float MorphContext::Step(const MorphAnimation*, float a_ratio) { return a_ratio; }
	void MorphContext::Invalidate() {}
	void MorphAnimation::Sample(float, MorphContext&, const std::span<ozz::math::SimdFloat4>&) {}
}
