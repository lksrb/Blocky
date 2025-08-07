#pragma once

#define MAX_PERMUTATION_DATA 512

struct perlin_noise
{
    u8 PermutationData[MAX_PERMUTATION_DATA];
    random_series RandomSeries;
};

internal void perlin_noise_create(perlin_noise* PerlinNoise, u32 Seed)
{
    PerlinNoise->RandomSeries = random_series_create(Seed);
    // TODO: Utilize seed

    for (u32 Index = 0; Index < MAX_PERMUTATION_DATA / 2; ++Index)
    {
        PerlinNoise->PermutationData[Index] = (u8)random_series_u32(&PerlinNoise->RandomSeries, 255);
    }

    // Copy first half
    for (u32 Index = MAX_PERMUTATION_DATA / 2; Index < MAX_PERMUTATION_DATA; ++Index)
    {
        PerlinNoise->PermutationData[Index] = PerlinNoise->PermutationData[Index - MAX_PERMUTATION_DATA / 2];
    }
}

internal f32 perlin_noise_get(perlin_noise* PerlinNoise, f32 X, f32 Y)
{
    auto& p = PerlinNoise->PermutationData;

    auto fade = [](f32 t) -> f32
    {
        return t * t * t * (t * (t * 6 - 15) + 10);
    };

    auto grad = [](i32 hash, f32 x, f32 y) -> f32
    {
        i32 h = hash & 15;
        f32 u = h < 8 ? x : y;
        f32 v = h < 4 ? y : (h == 12 || h == 14 ? x : 0);
        return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
    };

    i32 FX = (i32)floor(X) & 255;
    i32 FY = (i32)floor(Y) & 255;

    f32 xf = X - floor(X);
    f32 yf = Y - floor(Y);

    f32 u = fade(xf);
    f32 v = fade(yf);

    i32 aa = p[p[FX] + FY];
    i32 ab = p[p[FX] + FY + 1];
    i32 ba = p[p[FX + 1] + FY];
    i32 bb = p[p[FX + 1] + FY + 1];

    f32 x1 = bkm::Lerp(grad(aa, xf, yf), grad(ba, xf - 1, yf), u);
    f32 x2 = bkm::Lerp(grad(ab, xf, yf - 1), grad(bb, xf - 1, yf - 1), u);

    return (bkm::Lerp(x1, x2, v) + 1.0f) / 2.0f; // normalize to [0,1]
}

