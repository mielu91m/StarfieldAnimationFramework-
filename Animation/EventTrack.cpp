#include "EventTrack.h"
#include <algorithm>
#include <cmath>

namespace Animation
{
	size_t EventTrack::GetSize() const
	{
		return sizeof(std::vector<Key>) + (sizeof(Key) * keys.size());
	}

	void EventTrack::InitKeys(float a_duration)
	{
		if (keys.empty()) return;
		if (a_duration <= 0.0f) {
			keys.clear();
			return;
		}
		float durMagnitude = 1.0f / a_duration;
		for (auto& k : keys)
			k.time *= durMagnitude;
		std::sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) { return a.time < b.time; });

		Key* prevKey = nullptr;
		for (auto iter = keys.begin(); iter != keys.end();) {
			auto& k = *iter;
			if (prevKey != nullptr && std::fabs(k.time - prevKey->time) < 0.001f && k.eventName == prevKey->eventName && k.arg == prevKey->arg) {
				iter = keys.erase(iter);
			} else {
				prevKey = &k;
				++iter;
			}
		}
	}

	void EventTrack::SampleEvents(float a_currentTime, float a_timeStep, IAnimEventHandler* a_eventHandler)
	{
		if (keys.empty() || a_timeStep == 0.0f || !a_eventHandler) return;
		bool forwards = a_timeStep > 0.0f;
		float prevTime = a_currentTime - a_timeStep;
		float flr = std::floor(prevTime);
		bool looped = (flr != 0.0f);
		prevTime -= flr;
		for (auto& k : keys) {
			if ((forwards && (looped ? (k.time <= a_currentTime || k.time > prevTime) : (k.time <= a_currentTime && k.time > prevTime))) ||
				(!forwards && (looped ? (k.time >= a_currentTime || k.time < prevTime) : (k.time >= a_currentTime && k.time < prevTime)))) {
				a_eventHandler->QueueEvent(k.eventName, k.arg);
			}
		}
	}
}
