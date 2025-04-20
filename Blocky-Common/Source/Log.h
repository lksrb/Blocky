#pragma once

// Log
#include <stdio.h>
#define BK_RESET_COLOR "\033[0m"
#define BK_GREEN_COLOR "\033[32m"
#define BK_YELLOW_COLOR "\033[33m"
#define BK_RED_COLOR "\033[31m"
#define BK_WHITE_RED_BG_COLOR "\033[41;37m"

#define Trace(...) do { \
    printf(__VA_ARGS__); \
    printf(BK_RESET_COLOR "\n"); \
} while(0)

#define Info(...) do { \
    printf(BK_GREEN_COLOR); \
    printf(__VA_ARGS__); \
    printf(BK_RESET_COLOR "\n"); \
} while(0)

#define Warn(...) do { \
    printf(BK_YELLOW_COLOR); \
    printf(__VA_ARGS__); \
    printf(BK_RESET_COLOR "\n"); \
} while(0)

#define Err(...) do { \
    printf(BK_WHITE_RED_BG_COLOR); \
    printf(__VA_ARGS__); \
    printf(BK_RESET_COLOR "\n"); \
} while(0)

#define TraceV3(__V3) Trace("(%.3f, %.3f, %.3f)", __V3.x, __V3.y, __V3.z)
#define InfoV3(__V3) Info("(%.3f, %.3f, %.3f)", __V3.x, __V3.y, __V3.z)
