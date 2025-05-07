--
-- Workspace
--

-- Imports a table with include directories, library directories and binary directories
include "Dependencies"

-- Includes curstom premake command for adding solution items (VS only)
include "./Dependencies/premake/customizations/solution_items.lua"

workspace "Blocky"
	architecture "x86_64"
	startproject "Blocky"

	configurations {
	    "Debug",
        "Release"
    }

	solution_items {
		".editorconfig",
		".gitignore",
		"Dependencies.lua",
		"premake5.lua",
		"Resources/Quad.hlsl",
		"Resources/Cuboid.hlsl",
		"Resources/QuadedCuboid.hlsl",
		"Resources/FastCuboid.hlsl",
		"Blocky/Blocky.lua",
		"Blocky-ResourcePacker/Blocky-ResourcePacker.lua",
		"Resources/Light.hlsl",
	}

	flags {
		"MultiProcessorCompile"
	}

-- Output directory for binaries
outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}";

-- Main executable
group ""
include "Blocky/Blocky.lua"

-- Tools
group "Tools"
include "Blocky-ResourcePacker/Blocky-ResourcePacker.lua"

group "Common"
include "Blocky-Common/Blocky-Common.lua"
