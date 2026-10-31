#include "RealTransform.h"
#include <cmath>

namespace Animation
{
	RealTransform::RealTransform() = default;

	RealTransform::RealTransform(const RE::NiQuaternion& r, const RE::NiPoint3& t) : rotate(r), translate(t) {}

	RealTransform::RealTransform(const RE::NiTransform& t) { FromReal(t); }

	void RealTransform::FromOzz(const ozz::math::Transform& t)
	{
		rotate.x = t.rotation.x;
		rotate.y = t.rotation.y;
		rotate.z = t.rotation.z;
		rotate.w = t.rotation.w;
		translate.x = t.translation.x;
		translate.y = t.translation.y;
		translate.z = t.translation.z;
	}

	void RealTransform::ToReal(RE::NiTransform& t) const
	{
		rotate.ToMatrix(t.rotate);
		t.translate = translate;
	}

	void RealTransform::FromReal(const RE::NiTransform& t)
	{
		rotate.FromMatrix(t.rotate);
		translate = t.translate;
	}

	bool RealTransform::IsIdentity() const
	{
		return std::fabs(translate.x) < 0.0001f && std::fabs(translate.y) < 0.0001f && std::fabs(translate.z) < 0.0001f &&
			std::fabs(rotate.w - 1.0f) < 0.0001f && std::fabs(rotate.x) < 0.0001f && std::fabs(rotate.y) < 0.0001f && std::fabs(rotate.z) < 0.0001f;
	}

	void RealTransform::MakeIdentity()
	{
		translate.x = translate.y = translate.z = 0.0f;
		rotate.w = 1.0f;
		rotate.x = rotate.y = rotate.z = 0.0f;
	}

	RealTransform RealTransform::operator-(const RealTransform& rhs) const
	{
		RealTransform r;
		r.rotate = rotate * rhs.rotate.InvertVector();
		r.translate = translate - rhs.translate;
		return r;
	}

	RealTransform RealTransform::operator*(const RealTransform& rhs) const
	{
		RealTransform r;
		r.rotate = rotate * rhs.rotate;
		r.translate = translate + rhs.translate;
		return r;
	}
}
