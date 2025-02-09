project "Blocky-ResourcePacker"
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
		"%{IncludeDir.stb}",
		"%{IncludeDir.VulkanSDK}"
	}

    links {
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

		defines {
  		   "BK_PLATFORM_WIN32"
		}

	filter "configurations:Debug"
		kind "ConsoleApp"
		defines { "BK_DEBUG" }
		runtime "Debug"
		symbols "on"

		links {
			"%{Library.ShaderC_Debug}",
			"%{Library.SPIRV_Cross_Debug}",
			"%{Library.SPIRV_Cross_GLSL_Debug}",
		}

		-- Copy DDLs next to  executable
		postbuildcommands {
			"if not exist %{cfg.targetdir}/shaderc_sharedd.dll ( {COPYFILE} %{LibraryDir.VulkanSDK_DLL}/shaderc_sharedd.dll %{cfg.targetdir} )",
			"if not exist %{cfg.targetdir}/spirv-cross-c-shared.dll ( {COPYFILE} %{LibraryDir.VulkanSDK_DLL}/spirv-cross-c-sharedd.dll %{cfg.targetdir} )",
			"if not exist %{cfg.targetdir}/SPIRV-Tools-sharedd.dll ( {COPYFILE} %{LibraryDir.VulkanSDK_DLL}/SPIRV-Tools-sharedd.dll %{cfg.targetdir} )",
			"start /D %{wks.location} %{cfg.targetdir}/%{prj.name}"
		}

	filter "configurations:Release"
		kind "ConsoleApp"
		defines { "BK_RELEASE" }
		runtime "Release"
		optimize "Speed"
		symbols "on"
		flags { "LinkTimeOptimization" }

		links {
			"%{Library.ShaderC_Release}",
			"%{Library.SPIRV_Cross_Release}",
			"%{Library.SPIRV_Cross_GLSL_Release}",
		}

		-- Copy DDLs next to executable
		postbuildcommands {
			"if not exist %{cfg.targetdir}/shaderc_shared.dll ( {COPYFILE} %{LibraryDir.VulkanSDK_DLL}/shaderc_shared.dll %{cfg.targetdir} )",
			"if not exist %{cfg.targetdir}/spirv-cross-c-shared.dll ( {COPYFILE} %{LibraryDir.VulkanSDK_DLL}/spirv-cross-c-shared.dll %{cfg.targetdir} )",
			"if not exist %{cfg.targetdir}/SPIRV-Tools-shared.dll ( {COPYFILE} %{LibraryDir.VulkanSDK_DLL}/SPIRV-Tools-shared.dll %{cfg.targetdir} )",
			"start /D %{wks.location} %{cfg.targetdir}/%{prj.name}"
		}