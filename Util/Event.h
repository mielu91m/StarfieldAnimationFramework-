#pragma once

#include <vector>
#include <algorithm>
#include <atomic>
#include "Util/General.h"

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

	// NAF-style base for listeners
	template <typename E>
	class ListenerBase
	{
	public:
		virtual ListenerStatus OnEvent(E&) = 0;
		virtual ~ListenerBase() = default;
	};

	// NAF-style Dispatcher (thread-safe with Guarded)
	template <typename E>
	class Dispatcher
	{
	public:
		void AddListener(ListenerBase<E>* a_listener)
		{
			auto d = _data.lock();
			auto it = std::find(d->listeners.begin(), d->listeners.end(), a_listener);
			if (it == d->listeners.end())
				d->listeners.push_back(a_listener);
		}

		void RemoveListener(ListenerBase<E>* a_listener)
		{
			auto d = _data.lock();
			auto it = std::find(d->listeners.begin(), d->listeners.end(), a_listener);
			if (it != d->listeners.end())
				d->listeners.erase(it);
		}

		void SendEvent(E a_event)
		{
			auto d = _data.lock();
			for (auto it = d->listeners.begin(); it != d->listeners.end();) {
				if ((*it)->OnEvent(a_event) == ListenerStatus::kRemove)
					it = d->listeners.erase(it);
				else
					++it;
			}
		}

	private:
		struct InternalData { std::vector<ListenerBase<E>*> listeners; };
		Util::Guarded<InternalData> _data;
	};

	template <typename E>
	class Listener : public ListenerBase<E>
	{
	public:
		using ListenerStatus = Util::Event::ListenerStatus;

		void RegisterForEvent(Dispatcher<E>* a_dispatcher)
		{
			UnregisterForEvent();
			_eventDispatcher = a_dispatcher;
			a_dispatcher->AddListener(this);
		}

		void UnregisterForEvent()
		{
			auto p = _eventDispatcher.load();
			if (p) {
				p->RemoveListener(this);
				_eventDispatcher = nullptr;
			}
		}

		~Listener() override { UnregisterForEvent(); }

	protected:
		std::atomic<Dispatcher<E>*> _eventDispatcher{ nullptr };
	};
}