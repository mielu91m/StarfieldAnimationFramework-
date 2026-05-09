#pragma once

#include <DirectXMath.h>

namespace Animation
{
    // Używamy DirectXMath, ponieważ Starfield (RE) natywnie korzysta z tych typów
    using Vector3 = DirectX::XMFLOAT3;
    using Quaternion = DirectX::XMFLOAT4;

    class Transform
    {
    public:
        Transform() : 
            translation(0.0f, 0.0f, 0.0f), 
            rotation(0.0f, 0.0f, 0.0f, 1.0f), 
            scale(1.0f) 
        {}

        // Podstawowe składniki transformacji w Starfield
        Vector3 translation;
        Quaternion rotation;
        float scale;

        // Metoda pomocnicza do tworzenia macierzy, jeśli będzie potrzebna w API_Internal
        DirectX::XMMATRIX ToMatrix() const {
            return DirectX::XMMatrixAffineTransformation(
                DirectX::XMVectorSet(scale, scale, scale, 0.0f),
                DirectX::XMVectorZero(),
                DirectX::XMLoadFloat4(&rotation),
                DirectX::XMLoadFloat3(&translation)
            );
        }
    };
}