#pragma once

#include "PCH.h"
#include <span>
#include <ozz/base/maths/soa_transform.h>

namespace Animation
{
	class PoseCache
	{
	public:
		class Handle
		{
		public:
			Handle(const Handle&) = delete;
			Handle(Handle&&) noexcept;
			Handle& operator=(const Handle&) = delete;
			Handle& operator=(Handle&&) noexcept;

			Handle() = default;
			~Handle();

			std::span<ozz::math::SoaTransform> get();
			ozz::span<ozz::math::SoaTransform> get_ozz();
			void reset();
			bool is_valid() const;

		private:
			friend class PoseCache;
			Handle(PoseCache* a_owner, size_t a_idx);

			PoseCache* _owner = nullptr;
			size_t _impl = UINT64_MAX;
		};

		void set_pose_size(size_t a_size);
		void reserve(size_t a_numPoses);
		Handle acquire_handle();
		size_t transforms_capacity() const;

	private:
		friend class Handle;
		void release_handle(size_t a_idx);
		std::span<ozz::math::SoaTransform> get_span(size_t a_idx);
		ozz::span<ozz::math::SoaTransform> get_span_ozz(size_t a_idx);

		size_t _pose_size = 0;
		std::vector<size_t> _freeIdxs;
		std::vector<ozz::math::SoaTransform> _cache;
	};
}
