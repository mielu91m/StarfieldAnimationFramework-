#pragma once

#include <vector>
#include <algorithm>

namespace Util::Event
{
	enum class ListenerStatus
	{
		kUnchanged,
		kRemove
	};

	// Klasa bazowa dla obiektów nasłuchujących
	template <typename T>
	class Sink
	{
	public:
		virtual ~Sink() = default;
		virtual ListenerStatus OnEvent(T& a_event) = 0;
	};

	// Klasa zarządzająca zdarzeniami (Source)
	template <typename T>
	class Event
	{
	public:
		void AddSink(Sink<T>* a_sink)
		{
			if (a_sink && std::find(_sinks.begin(), _sinks.end(), a_sink) == _sinks.end()) {
				_sinks.push_back(a_sink);
			}
		}

		void SendEvent(T& a_event)
		{
			for (auto it = _sinks.begin(); it != _sinks.end();) {
				if ((*it)->OnEvent(a_event) == ListenerStatus::kRemove) {
					it = _sinks.erase(it);
				} else {
					++it;
				}
			}
		}

	private:
		std::vector<Sink<T>*> _sinks;
	};
}