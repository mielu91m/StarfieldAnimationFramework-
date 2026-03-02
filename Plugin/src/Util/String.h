#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <filesystem>
#include <optional>
#include <RE/Starfield.h>

namespace Util::String
{
    // Pobiera ścieżkę do folderu Data gry
    inline std::filesystem::path GetDataPath()
    {
        return std::filesystem::path("Data");
    }

    inline bool StartsWith(std::string_view a_str, std::string_view a_prefix)
    {
        return a_str.size() >= a_prefix.size() && 
               a_str.compare(0, a_prefix.size(), a_prefix) == 0;
    }

    // Ścieżka do animacji SAF: Data/SAF/Animations
    inline std::filesystem::path GetAnimationsPath()
    {
        return GetDataPath() / "SAF" / "Animations";
    }

    // Rozwiązuje ścieżkę animacji. Jeśli path zawiera '/' lub '\\' lub zaczyna od "Data" – używa jako pełnej.
    // W przeciwnym razie: GetAnimationsPath() / path (np. "R1" -> Data/SAF/Animations/R1)
    inline std::filesystem::path ResolveAnimationPath(std::string_view a_path)
    {
        std::string p(a_path);
        if (p.find('/') != std::string::npos || p.find('\\') != std::string::npos ||
            StartsWith(a_path, "Data"))
            return std::filesystem::path(p);
        return GetAnimationsPath() / p;
    }

    inline std::string ToLower(std::string_view a_str)
    {
        std::string result(a_str);
        std::transform(result.begin(), result.end(), result.begin(),
            [](unsigned char c) { return std::tolower(c); });
        return result;
    }

    // Dodano konwertery typów
    inline std::optional<int32_t> StrToInt(const std::string& a_str)
    {
        try { return std::stoi(a_str); } catch (...) { return std::nullopt; }
    }

    inline std::optional<float> StrToFloat(const std::string& a_str)
    {
        try { return std::stof(a_str); } catch (...) { return std::nullopt; }
    }

    inline std::string FromFixedString(const RE::BSFixedString& a_fixed)
    {
        return a_fixed.c_str() ? a_fixed.c_str() : "";
    }

    inline std::vector<std::string> Split(const std::string& a_str, char a_delimiter)
    {
        std::vector<std::string> tokens;
        std::string token;
        std::istringstream tokenStream(a_str);
        while (std::getline(tokenStream, token, a_delimiter)) {
            tokens.push_back(token);
        }
        return tokens;
    }
}