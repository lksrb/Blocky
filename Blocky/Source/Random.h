#pragma once

#include <stdlib.h>

struct random_series
{
    u32 Seed;
};

// Global seed
global u32 g_RandomSeed = 0;

global void random_set_seed(u32 Seed)
{
    g_RandomSeed = Seed;

    Info("[Random] Set seed to %u.", Seed);
}

// Fast implementation of pseudo-random numbers
// This is used mostly in shaders but I think we can use it here aswell.
global u32 random_pcg_hash(u32 Input)
{
    u32 State = Input * 747796405u + 2891336453u;
    u32 Word = ((State >> ((State >> 28u) + 4u)) ^ State) * 277803737u;
    return (Word >> 22u) ^ Word;
}

global u32 random_u32(u32 Min = 0, u32 Max = UINT32_MAX)
{
    g_RandomSeed = random_pcg_hash(g_RandomSeed);

    // FIXME: Due to modulo we lose uniformness
    return Min + g_RandomSeed % (Max - Min);
}

// Test this
global u64 random_u64()
{
    u64 Value = 0;

    g_RandomSeed = random_pcg_hash(g_RandomSeed);

    Value |= g_RandomSeed;

    g_RandomSeed = random_pcg_hash(g_RandomSeed);

    Value |= (u64)g_RandomSeed << 32;

    return Value;
}

global random_series random_series_create(u32 Seed)
{
    random_series Series;
    Series.Seed = Seed;
    return Series;
}

global random_series random_series_create()
{
    random_series Series;
    Series.Seed = random_pcg_hash(g_RandomSeed);
    return Series;
}

global u32 random_series_u32(random_series* Series, u32 Min = 0, u32 Max = UINT32_MAX)
{
    Series->Seed = random_pcg_hash(Series->Seed);

    // FIXME: Due to modulo we lose uniformness
    return Min + Series->Seed % (Max + 1 - Min);
}

global f32 random_float01(random_series* Series)
{
    Series->Seed = random_pcg_hash(Series->Seed);
    constexpr f32 Scale = f32(1.0f / UINT32_MAX);
    return f32(Series->Seed) * Scale;
}

global v2 random_normal(random_series* Series)
{
    // NOTE: Not uniform anymore since y axis depends on x axis
    v2 Result;
    Result.x = 2.0f * random_float01(Series) - 1.0f; // [-1, 1]
    Result.y = 2.0f * random_float01(Series) - 1.0f; // [-1, 1]
    return Result;
}

global v2 random_direction(random_series* Series)
{
    v2 Result = random_normal(Series);
    Result.x = Result.x > 0.0f ? 1.0f : -1.0f;
    Result.y = Result.y > 0.0f ? 1.0f : -1.0f;
    return Result;
}
