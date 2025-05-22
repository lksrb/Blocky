#pragma once

#include <stdlib.h>

struct random_series
{
    u32 Seed;
};

// Global seed
global u32 g_RandomSeed = 0;

global void RandomSetSeed(u32 Seed)
{
    g_RandomSeed = Seed;

    Info("[Random] Set seed to %u.", Seed);
}

// Fast implementation of pseudo-random numbers
// This is used mostly in shaders but I think we can use it here aswell.
global u32 RandomPCGHash(u32 Input)
{
    u32 State = Input * 747796405u + 2891336453u;
    u32 Word = ((State >> ((State >> 28u) + 4u)) ^ State) * 277803737u;
    return (Word >> 22u) ^ Word;
}

global u32 RandomU32(u32 Min = 0, u32 Max = UINT32_MAX)
{
    g_RandomSeed = RandomPCGHash(g_RandomSeed);

    // FIXME: Due to modulo we lose uniformness
    return Min + g_RandomSeed % (Max + 1 - Min);
}

// Test this
global u64 RandomU64()
{
    u64 Value = 0;

    g_RandomSeed = RandomPCGHash(g_RandomSeed);

    Value |= g_RandomSeed;

    g_RandomSeed = RandomPCGHash(g_RandomSeed);

    Value |= (u64)g_RandomSeed << 32;

    return Value;
}

global random_series RandomSeriesCreate()
{
    random_series Series;
    Series.Seed = RandomPCGHash(g_RandomSeed);
    return Series;
}

global f32 RandomFloat01(random_series* Series)
{
    Series->Seed = RandomPCGHash(Series->Seed);
    constexpr f32 Scale = f32(1.0f / UINT32_MAX);
    return f32(Series->Seed) * Scale;
}

global v2 RandomNormal(random_series* Series)
{
    // NOTE: Not uniform anymore since y axis depends on x axis
    v2 Result;
    Result.x = 2.0f * RandomFloat01(Series) - 1.0f; // [-1, 1]
    Result.y = 2.0f * RandomFloat01(Series) - 1.0f; // [-1, 1]
    return Result;
}

global v2 RandomDirection(random_series* Series)
{
    v2 Result = RandomNormal(Series);
    Result.x = Result.x > 0.0f ? 1.0f : -1.0f;
    Result.y = Result.y > 0.0f ? 1.0f : -1.0f;
    return Result;
}

// UUID
// TODO: This is a temporary location until we figure out where this belongs
struct uuid
{
    u64 Value;
};

global uuid UUIDCreate()
{
    uuid UUID;
    UUID.Value = RandomU64(); // Now this might be silly but we will test, if it works
    return UUID;
}
