
-- Dependencies

VULKAN_SDK = os.getenv("VULKAN_SDK")

IncludeDir = {}

IncludeDir["Blocky_Common"] =					"%{wks.location}/Blocky-Common/Source"
IncludeDir["VulkanSDK"] =						"%{VULKAN_SDK}/Include"
IncludeDir["Assimp"] =							"%{wks.location}/dependencies/assimp/include"
IncludeDir["stb"] =								"%{wks.location}/dependencies/stb"

LibraryDir = {}
LibraryDir["VulkanSDK"] =						"%{VULKAN_SDK}/Lib"
LibraryDir["VulkanSDK_DLL"] =					"%{VULKAN_SDK}/Bin"

Library = {}
Library["Vulkan"] =								"%{LibraryDir.VulkanSDK}/vulkan-1.lib"
Library["VulkanUtils"] =						"%{LibraryDir.VulkanSDK}/VkLayer_utils.lib"

Library["ShaderC_Debug"] =						"%{LibraryDir.VulkanSDK}/shaderc_sharedd.lib"
Library["ShaderC_Release"] =					"%{LibraryDir.VulkanSDK}/shaderc_shared.lib"

Library["SPIRV_Cross_Debug"] =					"%{LibraryDir.VulkanSDK}/spirv-cross-cored.lib"
Library["SPIRV_Cross_Release"] =				"%{LibraryDir.VulkanSDK}/spirv-cross-core.lib"

Library["SPIRV_Cross_GLSL_Debug"] =				"%{LibraryDir.VulkanSDK}/spirv-cross-glsld.lib"
Library["SPIRV_Cross_GLSL_Release"]=			"%{LibraryDir.VulkanSDK}/spirv-cross-glsl.lib"

Library["Assimp_Debug"] = "%{wks.location}/dependencies/assimp/bin/Debug/assimp-vc143-mtd.lib"
Library["Assimp_Release"] = "%{wks.location}/dependencies/assimp/bin/Release/assimp-vc143-mt.lib"

Binaries = {}
Binaries["Assimp_Debug"] = "%{wks.location}/dependencies/assimp/bin/Debug/assimp-vc143-mtd.dll"
Binaries["Assimp_Release"] = "%{wks.location}/dependencies/assimp/bin/Release/assimp-vc143-mt.dll"