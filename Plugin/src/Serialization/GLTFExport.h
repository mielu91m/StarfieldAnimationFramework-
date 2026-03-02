#pragma once

#include <vector>
#include <cstdint>
#include <stddef.h> // Dla std::byte

// Deklaracje wyprzedzające - przyspieszają kompilację
namespace ozz {
    namespace animation {
        class Skeleton;
    }
}

namespace Animation {
    struct RawOzzAnimation;
}

namespace Serialization
{
    class GLTFExport
    {
    public:
        // level = 0 oznacza brak dodatkowej optymalizacji ozz
        static std::vector<std::byte> CreateOptimizedAsset(
            Animation::RawOzzAnimation* anim, 
            const ozz::animation::Skeleton* skeleton, 
            uint8_t level = 0
        );
    };
}