#include "Sequencer.h"
#include <algorithm>

namespace Animation::Sequencer
{
    PhaseSequenceRunner::PhaseSequenceRunner(phases_vector a_phases)
        : _phases(std::move(a_phases))
    {
        if (!_phases.empty()) {
            _loopsRemaining = _phases[_currentIndex].loopCount;
        }
    }

    size_t PhaseSequenceRunner::GetPhase() const
    {
        return _currentIndex;
    }

    void PhaseSequenceRunner::SetPhase(size_t idx)
    {
        if (idx >= _phases.size())
            return;
        _currentIndex = idx;
        _loopsRemaining = _phases[_currentIndex].loopCount;
    }

    std::optional<size_t> PhaseSequenceRunner::GetNextPhaseIndex() const
    {
        size_t next = _currentIndex + 1;
        if (next >= _phases.size()) {
            if (_loop)
                return 0;
            return std::nullopt;
        }
        return next;
    }

    void PhaseSequenceRunner::AdvancePhase(bool a_init)
    {
        if (_phases.empty())
            return;
        if (!a_init) {
            auto next = GetNextPhaseIndex();
            if (!next.has_value())
                return;
            _currentIndex = *next;
        }
        _loopsRemaining = _phases[_currentIndex].loopCount;
    }

    int32_t PhaseSequenceRunner::GetLoopsRemaining() const
    {
        return _loopsRemaining;
    }

    const PhaseData* PhaseSequenceRunner::GetCurrentPhase() const
    {
        if (_currentIndex >= _phases.size())
            return nullptr;
        return &_phases[_currentIndex];
    }

    const PhaseSequenceRunner::phases_vector& PhaseSequenceRunner::GetPhases() const
    {
        return _phases;
    }
}
