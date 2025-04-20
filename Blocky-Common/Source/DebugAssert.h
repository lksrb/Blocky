#pragma once

#define ENABLE_ASSERTS 1

#ifdef BK_DEBUG
#define Debugbreak() __debugbreak()
#else
#define Debugbreak()
#endif
#if ENABLE_ASSERTS
#define Assert(__cond__, ...) do { if(!(__cond__)) { Err(__VA_ARGS__); Debugbreak(); } } while(0)
#else
#define Assert(...)
#endif
