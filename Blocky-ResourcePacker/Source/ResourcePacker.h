#pragma once

// Optimize shaders
#if defined(BK_DEBUG) || defined(BK_RELEASE)
#define OPTIMIZE_SHADER 0
#elif defined(BK_DIST)
#define OPTIMIZE_SHADER 1
#define GENERATE_HEADER 1
#endif

// Standard library
#include <cstdio>
#include <string>
#include <fstream>
#include <filesystem>
#include <vector>

// Assimp
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/cimport.h>

// Silence unused function warning for stbi
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif

// STBI for texture loading and font rasterizing
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

//#define STB_TRUETYPE_IMPLEMENTATION
//#define STBTT_STATIC
//#include <stb_truetype.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

// Blocky common headers
#include "Common.h"
#include "ShaderWrite.h"
#include "MeshWrite.h"
#include "ModelWrite.h"
