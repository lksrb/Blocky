@echo off
del ..\Blocky\Blocky.vcxproj.filters
del ..\Blocky\Blocky.vcxproj.user
del ..\Blocky\Blocky.vcxproj

del ..\Blocky-Common\Blocky-Common.vcxproj.filters
del ..\Blocky-Common\Blocky-Common.vcxproj.user
del ..\Blocky-Common\Blocky-Common.vcxproj

del ..\Blocky-ResourcePacker\Blocky-ResourcePacker.vcxproj.filters
del ..\Blocky-ResourcePacker\Blocky-ResourcePacker.vcxproj.user
del ..\Blocky-ResourcePacker\Blocky-ResourcePacker.vcxproj

del ..\.vs
del ..\Blocky.sln
echo Cleaning solution files finished.
pause

