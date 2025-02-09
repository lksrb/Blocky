project "Blocky"
	language "C++"
	cppdialect "C++20"
	staticruntime "on"
	floatingpoint "fast"

	debugdir "%{wks.location}"
	editandcontinue "off"

	targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
	objdir ("%{wks.location}/bin-obj/" .. outputdir .. "/%{prj.name}")

	files {
		"Source/**.h",
		"Source/**.cpp",
	}

	includedirs {
		"Source",
		"%{IncludeDir.VulkanSDK}"
	}

	links {
		"%{Library.Vulkan}"
	}

	defines {
		"_CRT_SECURE_NO_WARNINGS",
		"NOMINMAX"
	}

	flags {
		"NoIncrementalLink"
	}

	filter "system:windows"
		systemversion "latest"
		defines { "BK_PLATFORM_WIN32" }

		-- Exclude all .cpp files from build by default
	filter "files:Source/**.cpp"
		flags "ExcludeFromBuild"

	-- List of files to build
	local includedFiles = {
	    "Source/Win32_Blocky.cpp"
	}

	-- Simply iterate over them and remove the set flag
	for _, file in ipairs(includedFiles) do
	    filter("files:" .. file)
	        removeflags "ExcludeFromBuild"
	end

	filter "configurations:Debug"
		kind "ConsoleApp"
		defines { "BK_DEBUG", "ENABLE_VALIDATION_LAYERS=1" }
		runtime "Debug"
		symbols "on"
		buildoptions { 
			"/Zc:nrvo-" -- Stops compiler from optimizing return values
		}

	filter "configurations:Release"
		kind "ConsoleApp"
		defines { "BK_RELEASE", "ENABLE_VALIDATION_LAYERS=0" }
		runtime "Release"
		optimize "Speed"
		symbols "on"
		flags { "LinkTimeOptimization" }
