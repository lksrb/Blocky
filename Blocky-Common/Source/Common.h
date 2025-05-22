#pragma once

// General defines
#define internal static
#define local_persist static
#define global static

#define CountOf(arr) sizeof(arr) / sizeof(arr[0])
#define STRINGIFY(x) #x

#define ENABLE_BITWISE_OPERATORS(Enum, SizeOfEnum)                   \
constexpr Enum operator|(Enum inLHS, Enum inRHS)                     \
{                                                                    \
    return Enum(static_cast<SizeOfEnum>(inLHS) |                     \
                static_cast<SizeOfEnum>(inRHS));                     \
}                                                                    \
                                                                     \
constexpr Enum operator&(Enum inLHS, Enum inRHS)                     \
{                                                                    \
    return Enum(static_cast<SizeOfEnum>(inLHS) &                     \
                static_cast<SizeOfEnum>(inRHS));                     \
}                                                                    \
                                                                     \
constexpr Enum operator^(Enum inLHS, Enum inRHS)                     \
{                                                                    \
    return Enum(static_cast<SizeOfEnum>(inLHS) ^                     \
                static_cast<SizeOfEnum>(inRHS));                     \
}                                                                    \
                                                                     \
constexpr Enum operator~(Enum inLHS)                                 \
{                                                                    \
    return Enum(~static_cast<SizeOfEnum>(inLHS));                    \
}                                                                    \
                                                                     \
constexpr Enum& operator|=(Enum& ioLHS, Enum inRHS)                  \
{                                                                    \
    ioLHS = ioLHS | inRHS;                                           \
    return ioLHS;                                                    \
}                                                                    \
                                                                     \
constexpr Enum& operator&=(Enum& ioLHS, Enum inRHS)                  \
{                                                                    \
    ioLHS = ioLHS & inRHS;                                           \
    return ioLHS;                                                    \
}                                                                    \
                                                                     \
constexpr Enum& operator^=(Enum& ioLHS, Enum inRHS)                  \
{                                                                    \
    ioLHS = ioLHS ^ inRHS;                                           \
    return ioLHS;                                                    \
}                                                                    \

#include "PrimitiveTypes.h"
#include "Log.h"
#include "DebugAssert.h"
#include "Math/BKM.h"

// Common types
struct buffer
{
    void* Data;
    u64 Size;
};

// Debug

// TODO: Make this cross platform
#ifdef WIN32_LEAN_AND_MEAN
class debug_scoped_timer
{
public:// 
    debug_scoped_timer(const char* name, bool output = true)
        : m_Name(name), m_Output(output)
    {
        ::QueryPerformanceCounter(&m_Start);
    }

    f32 Stop()
    {
        // Get counter
        LARGE_INTEGER end;
        ::QueryPerformanceCounter(&end);

        // Get frequency - should not change
        LARGE_INTEGER frequency;
        ::QueryPerformanceFrequency(&frequency);

        f32 timeStep = ((end.QuadPart - m_Start.QuadPart) / static_cast<f32>(frequency.QuadPart));
        return timeStep * 1000.0f;
    }

    ~debug_scoped_timer()
    {
        if (m_Output == false)
            return;

        // Get counter
        LARGE_INTEGER end;
        ::QueryPerformanceCounter(&end);

        // Get frequency - should not change
        LARGE_INTEGER frequency;
        ::QueryPerformanceFrequency(&frequency);

        f32 timeStep = ((end.QuadPart - m_Start.QuadPart) / static_cast<f32>(frequency.QuadPart));

        Trace("%s took %.5f ms", m_Name, timeStep * 1000.0f);
    }
private:
    LARGE_INTEGER m_Start;
    const char* m_Name;
    bool m_Output;
};

struct debug_cycle_counter
{
    u64 Begin;
    const char* Tag;

    debug_cycle_counter(const char* Tag)
    {
        this->Tag = Tag;
        Begin = __rdtsc();
    }

    ~debug_cycle_counter()
    {
        Trace("%s took %d cycles", Tag, i32(__rdtsc() - Begin));
    }
};

#endif

inline u32 SafeTruncateU64(u64 Value)
{
    Assert(Value <= UINT32_MAX, "Casting u64 that is bigger than u32!");
    return static_cast<u32>(Value);
}
