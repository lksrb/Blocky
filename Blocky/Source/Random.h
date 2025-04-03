#pragma once

#include <stdlib.h>

struct random_series
{
    u32 Seed;
};

// Global seed
internal inline u32 g_RandomSeed = 0;

internal void RandomSetSeed(u32 Seed)
{
    g_RandomSeed = Seed;

    Info("[Random] Set seed to %u.", Seed);
}

// Fast implementation of pseudo-random numbers
// This is used mostly in shaders but I think we can use it here aswell.
internal u32 RandomPCGHash(u32 Input)
{
    u32 State = Input * 747796405u + 2891336453u;
    u32 Word = ((State >> ((State >> 28u) + 4u)) ^ State) * 277803737u;
    return (Word >> 22u) ^ Word;
}

internal u32 RandomU32(u32 Min = 0, u32 Max = UINT32_MAX)
{
    g_RandomSeed = RandomPCGHash(g_RandomSeed);

    // FIXME: Due to modulo we lose uniformness
    return Min + g_RandomSeed % (Max + 1 - Min);
}

inline random_series RandomSeriesCreate()
{
    random_series Series;
    Series.Seed = RandomPCGHash(g_RandomSeed);
    return Series;
}

inline f32 RandomNormal(random_series* Series)
{
    Series->Seed = RandomPCGHash(Series->Seed);


}
