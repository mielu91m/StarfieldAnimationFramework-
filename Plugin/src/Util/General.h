#pragma once
#include "PCH.h"
#include <mutex>
#include <type_traits>

namespace Util
{
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