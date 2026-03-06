#include "ClipGenerator.h"
#include <ozz/base/maths/math_ex.h>
#include <cmath>

namespace Animation
{
	ClipGenerator::ClipGenerator(std::shared_ptr<OzzSkeleton> a_skeleton, ozz::unique_ptr<ozz::animation::Animation> a_anim) :
		_skeleton(std::move(a_skeleton)),
		_animation(std::move(a_anim)),
		_cache(_animation ? _animation->num_tracks() : 0)
	{
		if (_animation) {
			_locals.resize(_animation->num_soa_tracks());
		}
	}

	void ClipGenerator::SetDeltaTime(float a_dt)
	{
		_delta = a_dt;
	}

	void ClipGenerator::SetSpeed(float a_speed)
	{
		_speed = a_speed;
	}

	void ClipGenerator::SetLooping(bool a_loop)
	{
		_looping = a_loop;
	}

	float ClipGenerator::GetDuration() const
	{
		return _animation ? _animation->duration() : 0.0f;
	}

	bool ClipGenerator::IsFinished() const
	{
		if (!_animation || _looping) return false;
		return _time >= _animation->duration();
	}

	std::span<ozz::math::SoaTransform> ClipGenerator::Generate(IAnimEventHandler* a_eventHandler)
	{
		if (!_animation) {
			return _locals;
		}

		const float duration = _animation->duration();
		_time += _delta * _speed;
		if (duration > 0.0f) {
			if (_looping) {
				_time = std::fmod(_time, duration);
			} else {
				_time = std::min(_time, duration);
			}
		}

		const float ratio = (duration > 0.0f) ? (_time / duration) : 0.0f;

		ozz::animation::SamplingJob job;
		job.animation = _animation.get();
		job.context = &_cache;
		job.ratio = ratio;
		job.output = ozz::span<ozz::math::SoaTransform>(_locals.data(), _locals.size());
		job.Run();

		return _locals;
	}

	void ClipGenerator::SampleAtRatio(float ratio, std::vector<ozz::math::SoaTransform>& out)
	{
		if (!_animation || _locals.empty()) return;
		out.resize(_locals.size());
		ozz::animation::SamplingJob job;
		job.animation = _animation.get();
		job.context = &_cache;
		job.ratio = ratio;
		job.output = ozz::span<ozz::math::SoaTransform>(out.data(), out.size());
		job.Run();
	}

	size_t ClipGenerator::GetJointCount() const
	{
		return _skeleton ? _skeleton->jointNames.size() : 0;
	}

	const OzzSkeleton* ClipGenerator::GetSkeleton() const
	{
		return _skeleton.get();
	}
}
