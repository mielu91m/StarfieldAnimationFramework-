#pragma once

#include "Animation/Generator.h"
#include "Animation/Ozz.h"
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/sampling_job.h>

namespace Animation
{
	class ClipGenerator : public Generator
	{
	public:
		ClipGenerator(std::shared_ptr<OzzSkeleton> a_skeleton, ozz::unique_ptr<ozz::animation::Animation> a_anim);

		void SetDeltaTime(float a_dt);
		void SetSpeed(float a_speed);
		void SetLooping(bool a_loop);
		bool IsLooping() const { return _looping; }
		float GetSpeed() const { return _speed; }
		float GetDuration() const;
		float GetCurrentTime() const { return _time; }
		/// True when not looping and playback has reached the end
		bool IsFinished() const;

		std::span<ozz::math::SoaTransform> Generate(IAnimEventHandler* a_eventHandler) override;

		/// Sample animation at a given ratio [0,1] without advancing time. Used to get bind/rest pose (ratio=0).
		void SampleAtRatio(float ratio, std::vector<ozz::math::SoaTransform>& out);

		/// Bind (rest) pose the clip locals are relative to, per joint.
		/// Set from RawOzzAnimation::bindPose right after construction.
		/// The per-frame delta uses this as rest — never clip frame 0.
		void SetBindPose(std::vector<ozz::math::Transform> a_bindPose) { _bindPose = std::move(a_bindPose); }
		bool HasBindPose() const { return !_bindPose.empty(); }
		const std::vector<ozz::math::Transform>& GetBindPose() const { return _bindPose; }

		/// Up-axis conversion needed on write (GLB Y-up -> game Z-up).
		/// Set from RawOzzAnimation::needsYUpToZUpConversion. FBX world clips
		/// are already game-space and must skip conversion.
		void SetNeedsYUpToZUp(bool a_convert) { _needsYUpToZUp = a_convert; }
		bool NeedsYUpToZUp() const { return _needsYUpToZUp; }

		size_t GetJointCount() const;
		const OzzSkeleton* GetSkeleton() const;

	private:
		std::shared_ptr<OzzSkeleton> _skeleton;
		ozz::unique_ptr<ozz::animation::Animation> _animation;
		ozz::animation::SamplingJob::Context _cache;
		std::vector<ozz::math::SoaTransform> _locals;
		// File bind pose (see SetBindPose). Empty = unknown, fall back to skeleton rest.
		std::vector<ozz::math::Transform> _bindPose;
		// True = convert clip Y-up to game Z-up on write (see SetNeedsYUpToZUp).
		bool _needsYUpToZUp{ true };
		float _time{ 0.0f };
		float _delta{ 0.0f };
		float _speed{ 1.0f };
		bool _looping{ true };
	};
}
