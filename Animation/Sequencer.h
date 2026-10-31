#pragma once

#include <string>
#include <memory>
#include <optional>
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

        /// Logika faz sekwencji (bez FileManager/Graph) – odpowiednik NAF Sequencer.
        class PhaseSequenceRunner
        {
        public:
            using phases_vector = std::vector<PhaseData>;

            explicit PhaseSequenceRunner(phases_vector a_phases);

            size_t GetPhase() const;
            void SetPhase(size_t idx);
            std::optional<size_t> GetNextPhaseIndex() const;
            void AdvancePhase(bool a_init = false);
            int32_t GetLoopsRemaining() const;
            const PhaseData* GetCurrentPhase() const;
            const phases_vector& GetPhases() const;
            bool IsLoop() const { return _loop; }
            void SetLoop(bool v) { _loop = v; }

        private:
            phases_vector _phases;
            size_t _currentIndex = 0;
            int32_t _loopsRemaining = 0;
            bool _loop = false;
        };
    }

    struct SequencePhaseChangeEvent {
        RE::Actor* actor;
        std::string newPhaseFile;
    };
}