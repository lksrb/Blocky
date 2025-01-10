--
-- Workspace
--

-- Imports a table with include directories, library directories and binary directories
include "Dependencies"

-- Includes curstom premake command for adding solution items (VS only)
include "./Dependencies/premake/customizations/solution_items.lua"

workspace "CppProject"
	architecture "x86_64"
	startproject "CppProject"

	configurations {
	    "Debug",
        "Release"
    }

	solution_items {
		".editorconfig",
		".gitignore",
		"Dependencies.lua",
		"premake5.lua"
	}

	flags {
		"MultiProcessorCompile"
	}

-- Output directory for binaries
outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}";

-- Project
project "CppProject"
	language "C++"
	cppdialect "C++20"
	staticruntime "on"
	floatingpoint "fast"
	debugdir "%{wks.location}"

	targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-obj/" .. outputdir .. "/%{prj.name}")

	files {
		"Source/**.h",
		"Source/**.cpp",
	}

	includedirs {
		"Source"
	}

	defines {
		"_CRT_SECURE_NO_WARNINGS",
		"NOMINMAX"
	}

	filter "system:windows"
		systemversion "latest"
		defines { "GND_PLATFORM_WIN32" }
		links { }

		-- Exclude all .cpp files from build by default
	filter "files:Source/**.cpp"
		flags "ExcludeFromBuild"

	-- List of files to build
	local includedFiles = {
	    "Source/main.cpp"
	}

	-- Simply iterate over them and remove the set flag
	for _, file in ipairs(includedFiles) do
	    filter("files:" .. file)
	        removeflags "ExcludeFromBuild"
	end

	filter "configurations:Debug"
		kind "ConsoleApp"
		defines { "CPP_DEBUG", "ENABLE_VALIDATION_LAYERS=1" }
		runtime "Debug"
		symbols "on"

	filter "configurations:Release"
		kind "ConsoleApp"
		defines { "CPP_RELEASE", "ENABLE_VALIDATION_LAYERS=0" }
		runtime "Release"
		optimize "Speed"
		symbols "on"
		flags { "LinkTimeOptimization" }
