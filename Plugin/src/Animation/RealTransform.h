#pragma once

#include "PCH.h"
#include <array>
#include <functional>
#include <ozz/base/maths/transform.h>

namespace Animation
{
	struct RealTransform
	{
		RE::NiQuaternion rotate;
		RE::NiPoint3 translate;

		RealTransform();
		RealTransform(const RE::NiQuaternion& r, const RE::NiPoint3& t);
		RealTransform(const RE::NiTransform& t);

		void FromOzz(const ozz::math::Transform& t);
		void ToReal(RE::NiTransform& t) const;
		void FromReal(const RE::NiTransform& t);
		bool IsIdentity() const;
		void MakeIdentity();
		RealTransform operator-(const RealTransform& rhs) const;
		RealTransform operator*(const RealTransform& rhs) const;
	};
}
