#pragma once

#include "Animation/Generator.h"

namespace Animation
{
    // Lightweight ProceduralGenerator declaration so other modules can interact
    // with procedural generators without pulling in the whole procedural graph.
    class ProceduralGenerator : public Generator
    {
    public:
        virtual ~ProceduralGenerator() = default;

        GenType GetType() const override { return GenType::kProcedural; }

        // Iterate over blend/graph variables
        void ForEachVariable(const std::function<void(std::string_view, float&)>& a_cb) override { (void)a_cb; }

        bool SetVariable(std::string_view a_name, float a_value) override { (void)a_name; (void)a_value; return false; }

        float GetVariable(std::string_view a_name) override { (void)a_name; return 0.0f; }
    };
}
