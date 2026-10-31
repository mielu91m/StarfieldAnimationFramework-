#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>

namespace Animation
{
	// An AnimationManager that has not advanced its clock within this window is no longer a
	// reliable owner (the actor/manager may have unloaded while another participant remains live).
	inline constexpr std::int64_t kClockOwnerStaleMs = 250;

	// Advances a clock from a stream of (token, dt) update reports without a global frame counter.
	// Engine calls AnimationManager::Update several times per render frame per graph with subdivided dt.
	// Can be multiple managers so be careful of summing dt, which can cause over-advances.
	// The first manager reported becomes owner of FrameClock, and only it is responsible for advancing time.
	// If that manager stops reporting, the next live reporter takes ownership after a short grace window.
	struct FrameClock
	{
		const void* owner = nullptr;
		float time = 0.0f;
		std::int64_t lastAdvanceMs = 0;

		// True if this report's dt should be added to `time`
		// (token is owning AnimationManager)
		bool ShouldAdvance(const void* a_token)
		{
			const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch()).count();
			return ShouldAdvance(a_token, nowMs);
		}

		bool ShouldAdvance(const void* a_token, std::int64_t a_nowMs)
		{
			if (owner && owner != a_token && a_nowMs - lastAdvanceMs > kClockOwnerStaleMs) {
				owner = nullptr;
			}
			if (!owner) {
				owner = a_token;
			}
			if (a_token != owner) {
				return false;
			}
			lastAdvanceMs = a_nowMs;
			return true;
		}

		void Reset()
		{
			owner = nullptr;
			time = 0.0f;
			lastAdvanceMs = 0;
		}
	};

	// SyncGroup is effectively a shared clock for actors in sync group.
	// clock *does not* wrap; each graph wraps it by its own clip duration,
	// so mismatched-length clips still loop independently while sharing a phase origin.
	struct SyncGroup
	{
		std::mutex lock;
		FrameClock clock;

		// Playback speed for all members. Any member's SetSpeed sets it and the clock advances by it
		// regardless of which graph owns the clock
		std::atomic<float> speed{ 1.0f };
	};
}
