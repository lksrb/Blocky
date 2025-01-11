@echo off
setlocal

:: Check if VULKAN_SDK environment variable is set
if defined VULKAN_SDK (
    echo Vulkan SDK is already installed at %VULKAN_SDK%.
    goto :end
)

:: Set the version of Vulkan SDK to download
set "VULKAN_VERSION=1.3.290.0"
set "VULKAN_URL=https://sdk.lunarg.com/sdk/download/%VULKAN_VERSION%/windows/VulkanSDK-%VULKAN_VERSION%-Installer.exe"

:: Set the download destination
set "DOWNLOAD_PATH_DIR=..\Dependencies\Vulkan"
set "DOWNLOAD_PATH=%DOWNLOAD_PATH_DIR%\VulkanSDK-%VULKAN_VERSION%-Installer.exe"

:: Check if the installer already hasn't been installed
if exist "%DOWNLOAD_PATH%" (
    echo Installer already downloaded. Running the installer.
    "%DOWNLOAD_PATH%"
    goto :end
)

mkdir %DOWNLOAD_PATH_DIR%

:: Download the Vulkan SDK installer
echo Downloading Vulkan SDK version %VULKAN_VERSION%...
powershell -Command "Invoke-WebRequest -Uri %VULKAN_URL% -OutFile %DOWNLOAD_PATH%"

:: Check if the download was successful
if exist "%DOWNLOAD_PATH%" (
    echo Download successful. Running the installer...
    "%DOWNLOAD_PATH%"
) else (
    echo Failed to download Vulkan SDK.
    goto :end
)

:end
call Win32-GenerateSolution.bat nopause
echo Done.
endlocal
pause
