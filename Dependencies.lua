
-- Dependencies

IncludeDir = {}

IncludeDir["Blocky_Common"] =					"%{wks.location}/Blocky-Common/Source"
IncludeDir["Assimp"] =							"%{wks.location}/dependencies/assimp/include"
IncludeDir["stb"] =								"%{wks.location}/dependencies/stb"
IncludeDir["imgui"] =							"%{wks.location}/dependencies/imgui"

LibraryDir = {}

Library = {}
Library["Assimp_Debug"] = "%{wks.location}/dependencies/assimp/bin/Debug/assimp-vc143-mtd.lib"
Library["Assimp_Release"] = "%{wks.location}/dependencies/assimp/bin/Release/assimp-vc143-mt.lib"

Binaries = {}
Binaries["Assimp_Debug"] = "%{wks.location}/dependencies/assimp/bin/Debug/assimp-vc143-mtd.dll"
Binaries["Assimp_Release"] = "%{wks.location}/dependencies/assimp/bin/Release/assimp-vc143-mt.dll"