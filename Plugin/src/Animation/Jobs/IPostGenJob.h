#pragma once

#include "PCH.h"
#include <ozz/base/maths/soa_transform.h>
#include <ozz/animation/runtime/skeleton.h>

namespace Animation
{
	class IPostGenJob
	{
	public:
		class Owner
		{
		public:
			Owner(const Owner&) = delete;
			Owner& operator=(const Owner&) = delete;

			explicit Owner(IPostGenJob* a_ptr) : _impl(a_ptr) {}
			Owner(Owner&& a_other) noexcept : _impl(a_other.release()) {}
			Owner& operator=(Owner&& a_other) noexcept { _impl = a_other.release(); return *this; }

			IPostGenJob* get() const { return _impl; }
			IPostGenJob* operator->() const { return get(); }
			IPostGenJob* release() { IPostGenJob* p = _impl; _impl = nullptr; return p; }
			void reset(IPostGenJob* a_ptr = nullptr) { if (_impl) _impl->Destroy(); _impl = a_ptr; }
			~Owner() { reset(); }

		private:
			IPostGenJob* _impl = nullptr;
		};

		struct Context
		{
			ozz::math::SoaTransform* localTransforms = nullptr;
			uint64_t localCount = 0;
			ozz::math::Float4x4* modelSpaceMatrices = nullptr;
			uint64_t modelSpaceCount = 0;
			ozz::math::Float4x4 rootMatrix;
			ozz::math::Float4x4 prevRootMatrix;
			ozz::math::Float4x4 invertedRootMatrix;
			ozz::animation::Skeleton* skeleton = nullptr;
			float deltaTime = 0.0f;
		};

		union GUID
		{
			struct { uint32_t jobType; uint32_t instanceNum; } parts;
			uint64_t full;
		};

		virtual bool Run(const Context& a_context) = 0;
		virtual GUID GetGUID() = 0;
		virtual void Destroy() = 0;
		virtual ~IPostGenJob() = default;
	};
}
