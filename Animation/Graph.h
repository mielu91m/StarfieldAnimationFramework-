#pragma once
#include "PCH.h"

#include "Animation/Transform.h"

namespace Animation
{
	class IAnimEventHandler;  // Definicja w Generator.h

    class Node
    {
    public:
        std::string name;
        Transform local;
        Transform world;
    };

    class Graph
    {
    public:
        enum class FLAGS : uint32_t
        {
            kNone = 0,
            kActive = 1 << 0,
            kPaused = 1 << 1
        };

        Graph() = default;

        bool LoadGraph(RE::TESObjectREFR* a_ref) {
            if (!a_ref) return false;
            return true;
        }

        void PlayEvent(const RE::BSFixedString& a_eventName) {
        }

        float GetVariableFloat(const RE::BSFixedString& a_variableName) const { return 0.0f; }
        void SetVariableFloat(const RE::BSFixedString& a_variableName, float a_value) {}
        bool IsValid() const { return true; }
    };
}
