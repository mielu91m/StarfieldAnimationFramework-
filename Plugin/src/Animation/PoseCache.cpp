#include "PoseCache.h"

namespace Animation
{
	PoseCache::Handle::Handle(Handle&& a_rhs) noexcept { *this = std::move(a_rhs); }

	PoseCache::Handle::Handle(PoseCache* a_owner, size_t a_idx) : _owner(a_owner), _impl(a_idx) {}

	PoseCache::Handle::~Handle() { reset(); }

	PoseCache::Handle& PoseCache::Handle::operator=(Handle&& a_rhs) noexcept
	{
		reset();
		_impl = a_rhs._impl;
		_owner = a_rhs._owner;
		a_rhs._impl = UINT64_MAX;
		a_rhs._owner = nullptr;
		return *this;
	}

	std::span<ozz::math::SoaTransform> PoseCache::Handle::get()
	{
		return _owner ? _owner->get_span(_impl) : std::span<ozz::math::SoaTransform>();
	}

	ozz::span<ozz::math::SoaTransform> PoseCache::Handle::get_ozz()
	{
		return _owner ? _owner->get_span_ozz(_impl) : ozz::span<ozz::math::SoaTransform>();
	}

	void PoseCache::Handle::reset()
	{
		if (_owner && _impl != UINT64_MAX) {
			_owner->release_handle(_impl);
			_impl = UINT64_MAX;
			_owner = nullptr;
		}
	}

	bool PoseCache::Handle::is_valid() const { return _owner && _impl != UINT64_MAX; }

	void PoseCache::set_pose_size(size_t a_size) { _pose_size = a_size; }

	void PoseCache::reserve(size_t a_numPoses) { _cache.reserve(a_numPoses * _pose_size); }

	PoseCache::Handle PoseCache::acquire_handle()
	{
		if (!_freeIdxs.empty()) {
			size_t idx = _freeIdxs.back();
			_freeIdxs.pop_back();
			return Handle(this, idx);
		}
		size_t start = _cache.size();
		_cache.resize(start + _pose_size);
		return Handle(this, start);
	}

	size_t PoseCache::transforms_capacity() const { return _cache.capacity(); }

	void PoseCache::release_handle(size_t a_idx) { _freeIdxs.push_back(a_idx); }

	std::span<ozz::math::SoaTransform> PoseCache::get_span(size_t a_idx)
	{
		return std::span<ozz::math::SoaTransform>(&_cache[a_idx], _pose_size);
	}

	ozz::span<ozz::math::SoaTransform> PoseCache::get_span_ozz(size_t a_idx)
	{
		return ozz::span<ozz::math::SoaTransform>(&_cache[a_idx], _pose_size);
	}
}
