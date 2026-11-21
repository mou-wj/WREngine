#pragma once

#include "Math.hpp"

namespace Renderer
{
    // Density scales the per-world-unit optical coefficients. Emission is linear radiance per unit length.
    struct VolumeMedium
    {
        float Density = 0.08f;
        Core::Float3 Scattering{0.72f, 0.76f, 0.82f};
        Core::Float3 Absorption{0.18f, 0.18f, 0.18f};
        Core::Float3 Emission{0.0f, 0.0f, 0.0f};
        float Anisotropy = 0.0f;
    };
}
