#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <optional>
#include <cstring>
#include <Windows.h>
#include <psapi.h>  // Dla MODULEINFO i GetModuleInformation

namespace Util::Pattern
{
    // Konwertuje string patternu (np. "48 89 5C 24 08 ? ? ? ? 55") na maskę
    // ? lub ?? = wildcard (dowolny bajt)
    struct PatternData {
        std::vector<uint8_t> bytes;
        std::vector<bool> mask;  // true = sprawdź ten bajt, false = wildcard
    };

    inline PatternData ParsePattern(const char* a_pattern)
    {
        PatternData result;
        std::string pattern(a_pattern);
        
        for (size_t i = 0; i < pattern.size(); ) {
            // Pomiń spacje
            while (i < pattern.size() && pattern[i] == ' ') ++i;
            if (i >= pattern.size()) break;
            
            // Sprawdź czy wildcard
            if (pattern[i] == '?' || pattern[i] == '*') {
                result.bytes.push_back(0);
                result.mask.push_back(false);
                ++i;
                // Pomijaj drugi ? jeśli ??
                if (i < pattern.size() && pattern[i] == '?') ++i;
            } else {
                // Parsuj hex
                char hex[3] = { pattern[i], 0, 0 };
                if (i + 1 < pattern.size() && pattern[i+1] != ' ') {
                    hex[1] = pattern[i+1];
                    i += 2;
                } else {
                    hex[1] = '0';
                    i += 1;
                }
                
                uint8_t byte = static_cast<uint8_t>(std::strtol(hex, nullptr, 16));
                result.bytes.push_back(byte);
                result.mask.push_back(true);
            }
            
            // Pomiń spacje
            while (i < pattern.size() && pattern[i] == ' ') ++i;
        }
        
        return result;
    }

    // Szukaj patternu w pamięci
    inline std::optional<uintptr_t> Scan(const PatternData& a_pattern, 
                                         uintptr_t a_start, 
                                         size_t a_size)
    {
        if (a_pattern.bytes.empty()) return std::nullopt;
        
        const size_t patternLen = a_pattern.bytes.size();
        
        for (size_t i = 0; i <= a_size - patternLen; ++i) {
            bool match = true;
            
            for (size_t j = 0; j < patternLen; ++j) {
                if (a_pattern.mask[j]) {
                    uint8_t memByte = *reinterpret_cast<uint8_t*>(a_start + i + j);
                    if (memByte != a_pattern.bytes[j]) {
                        match = false;
                        break;
                    }
                }
            }
            
            if (match) {
                return a_start + i;
            }
        }
        
        return std::nullopt;
    }

    // Szukaj patternu w module (np. Starfield.exe)
    inline std::optional<uintptr_t> ScanInModule(const PatternData& a_pattern, 
                                                  const char* a_moduleName = nullptr)
    {
        HMODULE module = GetModuleHandleA(a_moduleName);
        if (!module) return std::nullopt;
        
        MODULEINFO modInfo;
        if (!GetModuleInformation(GetCurrentProcess(), module, &modInfo, sizeof(modInfo))) {
            return std::nullopt;
        }
        
        return Scan(a_pattern, reinterpret_cast<uintptr_t>(modInfo.lpBaseOfDll), modInfo.SizeOfImage);
    }

    // Helper: szukaj od razu z string pattern
    inline std::optional<uintptr_t> Scan(const char* a_patternStr, 
                                         uintptr_t a_start, 
                                         size_t a_size)
    {
        return Scan(ParsePattern(a_patternStr), a_start, a_size);
    }

    // Get module base and size
    inline bool GetModuleInfo(const char* a_moduleName, uintptr_t& a_base, size_t& a_size)
    {
        HMODULE module = GetModuleHandleA(a_moduleName);
        if (!module) return false;
        
        MODULEINFO modInfo;
        if (!GetModuleInformation(GetCurrentProcess(), module, &modInfo, sizeof(modInfo))) {
            return false;
        }
        
        a_base = reinterpret_cast<uintptr_t>(modInfo.lpBaseOfDll);
        a_size = modInfo.SizeOfImage;
        return true;
    }
}
