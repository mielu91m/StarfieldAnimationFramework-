#pragma once
#include <stdint.h>
#include <string_view>
#include <Windows.h>

namespace MCF
{
    // Czysta struktura bez STL - bezpieczna między DLL
    template <typename T>
    struct simple_array
    {
        T* data;
        uint64_t count;

        simple_array() : data(nullptr), count(0) {}
        uint64_t size() const { return count; }
        T& operator[](uint64_t idx) { return data[idx]; }
        const T& operator[](uint64_t idx) const { return data[idx]; }
    };

    struct simple_string_view
    {
        const char* data;
        uint64_t size;

        simple_string_view() : data(nullptr), size(0) {}
        simple_string_view(const char* a_str) : data(a_str), size(a_str ? std::string_view(a_str).size() : 0) {}
        simple_string_view(const char* d, uint64_t s) : data(d), size(s) {}
        simple_string_view(const std::string_view& sv) : data(sv.data()), size(sv.size()) {}
    };

    class ConsoleInterface
    {
    public:
        virtual ~ConsoleInterface() = default;
        virtual RE::NiPointer<RE::TESObjectREFR> GetSelectedReference() = 0;
        virtual RE::TESForm* HexStrToForm(const simple_string_view& a_str) = 0;
        virtual void PrintLn(const simple_string_view& a_txt) = 0;
        virtual void PreventDefaultPrint() = 0;
    };

    typedef void (*CommandCallback)(const simple_array<simple_string_view>& a_args, const char* a_fullString, ConsoleInterface* a_intfc);

    // ============================================================================
    // Dynamiczny import funkcji z MCF.dll
    // ============================================================================

    namespace detail
    {
        // Typ wskaźnika funkcji RegisterCommand eksportowanej przez MCF
        typedef void (*RegisterCommandFunc)(const char* a_name, CommandCallback a_func);

        inline RegisterCommandFunc GetRegisterCommandFunc()
        {
            static RegisterCommandFunc cached = nullptr;
            
            if (cached) {
                return cached;
            }

            // Próbuj załadować ModernCommandFramework.dll
            HMODULE mcfHandle = GetModuleHandleA("ModernCommandFramework.dll");
            
            if (!mcfHandle) {
                spdlog::warn("ModernCommandFramework.dll not loaded - attempting to load");
                mcfHandle = LoadLibraryA("ModernCommandFramework.dll");
            }

            if (!mcfHandle) {
                spdlog::error("Failed to load ModernCommandFramework.dll");
                return nullptr;
            }

            // Pobierz adres funkcji RegisterCommand
            cached = reinterpret_cast<RegisterCommandFunc>(
                GetProcAddress(mcfHandle, "RegisterCommand")
            );

            if (!cached) {
                spdlog::error("Failed to find RegisterCommand in ModernCommandFramework.dll");
            }

            return cached;
        }
    }

    /**
     * Rejestruje komendę konsolową przez Modern Command Framework
     * 
     * @param a_name Nazwa komendy (np. "saf")
     * @param a_func Callback wywoływany gdy komenda zostanie użyta
     * @return true jeśli MCF jest załadowane i komenda została zarejestrowana
     */
    inline bool RegisterCommand(const char* a_name, CommandCallback a_func)
    {
        auto registerFunc = detail::GetRegisterCommandFunc();
        
        if (!registerFunc) {
            spdlog::warn("MCF RegisterCommand not available - is ModernCommandFramework installed?");
            return false;
        }

        try {
            registerFunc(a_name, a_func);
            spdlog::info("Successfully registered command '{}' with MCF", a_name);
            return true;
        }
        catch (...) {
            spdlog::error("Exception when registering command '{}' with MCF", a_name);
            return false;
        }
    }

    /**
     * Sprawdza czy ModernCommandFramework jest dostępny
     */
    inline bool IsAvailable()
    {
        return detail::GetRegisterCommandFunc() != nullptr;
    }
}