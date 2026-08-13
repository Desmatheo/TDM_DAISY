#pragma once

#include <cmath>

static inline float fastInvSqrt(float number) noexcept
{
    // Sur un STM32H7 (Cortex-M7) avec FPU matériel (VSQRT.F32), 
    // l'instruction matérielle sqrtf() est beaucoup plus rapide que 
    // le hack "Fast Inverse Square Root" des années 90 qui casse le pipeline !
    return 1.0f / sqrtf(number);
}

static inline float fastSqrt(float x) noexcept
{
    return sqrtf(x);
}
