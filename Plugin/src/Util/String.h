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

    // Rozwiązuje ścieżkę animacji. Jeśli path zaczyna od "Data" – używa jako pełnej (względem CWD).
    // W przeciwnym razie: GetAnimationsPath() / path, także dla podfolderów (np. "R1" -> Data/SAF/Animations/R1, "subfolder/anim" -> Data/SAF/Animations/subfolder/anim).
    inline std::filesystem::path ResolveAnimationPath(std::string_view a_path)
    {
        std::string p(a_path);
        if (StartsWith(a_path, "Data"))
            return std::filesystem::path(p);
        return GetAnimationsPath() / std::filesystem::path(p);
    }

    // Szuka pliku animacji (.glb/.gltf/.saf) po nazwie (stem) w Data/SAF/Animations i we wszystkich podfolderach.
    // Zwraca ścieżkę do pierwszego znalezionego pliku, lub nullopt gdy brak. Porównanie stem bez rozróżniania wielkości liter.
    inline std::optional<std::filesystem::path> FindAnimationByStem(std::string_view a_stem)
    {
        const auto dir = GetAnimationsPath();
        if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir))
            return std::nullopt;
        std::string stemLower(a_stem);
        std::transform(stemLower.begin(), stemLower.end(), stemLower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
        try {
            for (const auto& e : std::filesystem::recursive_directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied)) {
                if (!e.is_regular_file()) continue;
                std::string ext = e.path().extension().string();
                if (ext.size() < 2) continue;
                for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (ext != ".glb" && ext != ".gltf" && ext != ".saf") continue;
                std::string fileStem = e.path().stem().string();
                std::transform(fileStem.begin(), fileStem.end(), fileStem.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
                if (fileStem == stemLower)
                    return e.path();
            }
        } catch (...) {}
        return std::nullopt;
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