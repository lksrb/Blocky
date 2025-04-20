project "Blocky-Common"
	kind "None"
	language "C++"
	cppdialect "C++20"

	files {
		"Source/**.h",
	}

	includedirs {
		"Source",
		"%{IncludeDir.stb}",
		"%{IncludeDir.VulkanSDK}",
		"%{IncludeDir.Assimp}",
		"%{IncludeDir.stb}"
	}

    links {
	}

	defines {
		"_CRT_SECURE_NO_WARNINGS",
		"NOMINMAX"
	}

	flags {
	}

	filter "system:windows"
		systemversion "latest"

		defines {
  		   "BK_PLATFORM_WIN32"
		}

	filter "configurations:Debug"
		defines { "BK_DEBUG" }
		runtime "Debug"
		symbols "on"

	filter "configurations:Release"
		defines { "BK_RELEASE" }
		runtime "Release"
		optimize "Speed"
		symbols "on"
