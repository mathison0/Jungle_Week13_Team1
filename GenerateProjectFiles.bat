@echo off
setlocal

set "ROOT_DIR=%~dp0"
set "PHYSX_ROOT=%ROOT_DIR%KraftonEngine\ThirdParty\PhysX"
set "PHYSX_BIN=%PHYSX_ROOT%\physx\bin\win.x86_64.vc143.md"

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

"%ROOT_DIR%Scripts\python\python.exe" "%ROOT_DIR%Scripts\GenerateProjectFiles.py" %*
pause
