#pragma once
#include "PCH.h"
#include <mutex>
#include <type_traits>
#include <variant>

template <typename T, typename Variant>
struct variant_index;

template <typename T, typename... Types>
struct variant_index<T, std::variant<Types...>>
{
	static constexpr std::size_t value = []() {
		std::size_t result = 0;
		bool found = ((std::is_same_v<T, Types> ? true : (++result, false)) || ...);
		return found ? result : static_cast<std::size_t>(-1);
	}();
};

template <typename T, typename Variant>
inline constexpr std::size_t variant_index_v = variant_index<T, Variant>::value;

namespace Util
{
	float GetRandomFloat(float a_min, float a_max);

	// Klasa pomocnicza do bezpiecznego dostępu wielowątkowego
    template <typename T>
    class Guarded {
    public:
        struct Guard {
            Guard(T& a_data, std::mutex& a_mutex) : _data(a_data), _lock(a_mutex) {}
            T* operator->() { return &_data; }
            T& operator*() { return _data; }
        private:
            T& _data;
            std::lock_guard<std::mutex> _lock;
        };
        Guard lock() { return Guard(_data, _mutex); }
    private:
        T _data;
        std::mutex _mutex;
    };

    // Klasa pomocnicza do hookowania funkcji wirtualnych
    template <typename T>
    class VFuncHook {
    public:
        using FunctionPtr = typename std::add_pointer<T>::type;
        
        VFuncHook() = default;
        
        // Konstruktor inicjalizujący (opcjonalny)
        VFuncHook(uint32_t, uintptr_t a_address, const char*, void*) {
             _original = reinterpret_cast<FunctionPtr>(a_address);
        }

        // Metoda do ustawiania hooka (symulacja/placeholder)
        void Hook(uint32_t, uintptr_t a_address, const char*, void*) {
            _original = reinterpret_cast<FunctionPtr>(a_address);
        }

        FunctionPtr GetOriginal() const { return _original; }

        template <typename... Args>
        auto operator()(Args&&... a_args) { 
            return _original(std::forward<Args>(a_args)...); 
        }

    private:
        FunctionPtr _original{ nullptr };
    };
}