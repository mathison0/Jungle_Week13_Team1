@echo off
setlocal

set "ROOT_DIR=%~dp0"
set "PHYSX_ROOT=%ROOT_DIR%KraftonEngine\ThirdParty\PhysX"
set "PHYSX_BIN=%PHYSX_ROOT%\physx\bin\win.x86_64.vc143.md"
set "NVCLOTH_ROOT=%ROOT_DIR%KraftonEngine\ThirdParty\NvCloth"
set "NVCLOTH_BIN=%NVCLOTH_ROOT%\bin\win.x86_64.vc143.md"
set "NVCLOTH_LIB=%NVCLOTH_ROOT%\lib\win.x86_64.vc143.md"

rem 프로젝트 파일 생성 전에 PhysX 4.1 VS2022 바이너리와 include 경로를 확인한다.
if not exist "%PHYSX_ROOT%\physx\include\PxPhysicsAPI.h" (
    echo PhysX 4.1 include path not found:
    echo   %PHYSX_ROOT%\physx\include\PxPhysicsAPI.h
    pause
    exit /b 1
)

if not exist "%PHYSX_ROOT%\pxshared\include" (
    echo PhysX pxshared include path not found:
    echo   %PHYSX_ROOT%\pxshared\include
    pause
    exit /b 1
)

for %%c in (debug release) do (
    for %%f in (
        PhysXExtensions_static_64.lib
        PhysXCooking_64.lib
        PhysXPvdSDK_static_64.lib
        PhysXVehicle_static_64.lib
        PhysX_64.lib
        PhysXCommon_64.lib
        PhysXFoundation_64.lib
    ) do if not exist "%PHYSX_BIN%\%%c\%%f" (
        echo PhysX 4.1 library not found:
        echo   %PHYSX_BIN%\%%c\%%f
        pause
        exit /b 1
    )
)

if not exist "%NVCLOTH_ROOT%\include\NvCloth\Cloth.h" (
    echo NvCloth 1.1.6 include path not found:
    echo   %NVCLOTH_ROOT%\include\NvCloth\Cloth.h
    pause
    exit /b 1
)

if not exist "%NVCLOTH_ROOT%\include\NvClothExt\ClothFabricCooker.h" (
    echo NvCloth extension include path not found:
    echo   %NVCLOTH_ROOT%\include\NvClothExt\ClothFabricCooker.h
    pause
    exit /b 1
)

if not exist "%NVCLOTH_LIB%\debug\NvClothDEBUG_x64.lib" (
    echo NvCloth debug library not found:
    echo   %NVCLOTH_LIB%\debug\NvClothDEBUG_x64.lib
    pause
    exit /b 1
)

if not exist "%NVCLOTH_BIN%\debug\NvClothDEBUG_x64.dll" (
    echo NvCloth debug DLL not found:
    echo   %NVCLOTH_BIN%\debug\NvClothDEBUG_x64.dll
    pause
    exit /b 1
)

if not exist "%NVCLOTH_LIB%\release\NvCloth_x64.lib" (
    echo NvCloth release library not found:
    echo   %NVCLOTH_LIB%\release\NvCloth_x64.lib
    pause
    exit /b 1
)

if not exist "%NVCLOTH_BIN%\release\NvCloth_x64.dll" (
    echo NvCloth release DLL not found:
    echo   %NVCLOTH_BIN%\release\NvCloth_x64.dll
    pause
    exit /b 1
)

"%ROOT_DIR%Scripts\python\python.exe" "%ROOT_DIR%Scripts\GenerateProjectFiles.py" %*
pause
