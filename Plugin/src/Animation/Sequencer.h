#pragma once

#include <string>
#include <memory>
#include <RE/Starfield.h>
#include <vector>

namespace Animation
{
    namespace Sequencer
    {
        // To musi być zdefiniowane przed użyciem w GraphManager
        struct PhaseData
        {
            std::string file;
            int32_t loopCount = 0;
            float transitionTime = 1.0f;
        };
    }

    struct SequencePhaseChangeEvent {
        RE::Actor* actor;
        std::string newPhaseFile;
    };
}