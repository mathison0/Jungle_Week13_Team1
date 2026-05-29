@echo off
setlocal

set "SOLUTION_DIR=%~dp0"
set "PROJECT_DIR=%SOLUTION_DIR%KraftonEngine"
set "BUILD_OUTPUT=%PROJECT_DIR%\Bin\Release"
set "RELEASE_DIR=%SOLUTION_DIR%ReleaseBuild"
set "RELEASE_BIN=%RELEASE_DIR%\Bin"
set "PHYSX_BIN=%PROJECT_DIR%\ThirdParty\PhysX\physx\bin\win.x86_64.vc143.md\release"

rem VS Developer 환경 로드. msbuild를 PATH에 등록한다.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set "VS_PATH=%%i"
if not defined VS_PATH (
    echo Visual Studio was not found.
    pause
    exit /b 1
)
call "%VS_PATH%\Common7\Tools\VsDevCmd.bat" -no_logo

echo ============================================
echo  Release Build Script
echo ============================================

rem Release 빌드는 PhysX 4.1 VS2022 release 바이너리를 사용한다.
for %%f in (
    PhysXExtensions_static_64.lib
    PhysXCooking_64.lib
    PhysXPvdSDK_static_64.lib
    PhysXVehicle_static_64.lib
    PhysX_64.lib
    PhysXCommon_64.lib
    PhysXFoundation_64.lib
) do if not exist "%PHYSX_BIN%\%%f" (
    echo PhysX 4.1 library not found:
    echo   %PHYSX_BIN%\%%f
    pause
    exit /b 1
)

if not exist "%PHYSX_BIN%\PhysX_64.dll" (
    echo PhysX 4.1 DLL not found:
    echo   %PHYSX_BIN%\PhysX_64.dll
    pause
    exit /b 1
)

if not exist "%PHYSX_BIN%\PhysXCooking_64.dll" (
    echo PhysX 4.1 Cooking DLL not found:
    echo   %PHYSX_BIN%\PhysXCooking_64.dll
    pause
    exit /b 1
)

rem 1. MSBuild로 Release x64 빌드.
echo.
echo [1/3] Building Release x64...
msbuild "%SOLUTION_DIR%KraftonEngine.sln" /p:Configuration=Release /p:Platform=x64 /m /v:minimal
if %ERRORLEVEL% neq 0 (
    echo BUILD FAILED
    pause
    exit /b 1
)

rem 2. 기존 ReleaseBuild 폴더 정리 후 패키징 폴더 재생성.
echo.
echo [2/3] Preparing output directory...
if exist "%RELEASE_DIR%" rmdir /s /q "%RELEASE_DIR%"
mkdir "%RELEASE_DIR%"
mkdir "%RELEASE_BIN%"

rem 3. 실행 파일, DLL, 런타임 리소스 복사.
echo.
echo [3/3] Copying files...

rem exe와 동봉 DLL은 Bin 하위에 둔다. PhysX/RmlUi/fmod/lua/fbx DLL은
rem vcxproj PostBuildEvent가 이미 Bin\Release로 복사해둔 것을 사용한다.
copy "%BUILD_OUTPUT%\KraftonEngine.exe" "%RELEASE_BIN%\" >nul
xcopy "%BUILD_OUTPUT%\*.dll" "%RELEASE_BIN%\" /y /q >nul

rem 리소스는 패키지 루트에 둔다. FPaths가 현재 작업 디렉터리 기준으로
rem Shaders/Content/Settings를 찾기 때문이다.
xcopy "%PROJECT_DIR%\Shaders" "%RELEASE_DIR%\Shaders\" /e /i /q >nul
xcopy "%PROJECT_DIR%\Content" "%RELEASE_DIR%\Content\" /e /i /q >nul
xcopy "%PROJECT_DIR%\Settings" "%RELEASE_DIR%\Settings\" /e /i /q >nul

rem 런처: CWD를 ReleaseBuild 루트로 맞춘 뒤 Bin의 exe를 실행한다.
(
echo @echo off
echo cd /d "%%~dp0"
echo start "" "%%~dp0Bin\KraftonEngine.exe"
) > "%RELEASE_DIR%\Play.bat"

echo.
echo ============================================
echo  Build complete: %RELEASE_DIR%
echo ============================================
echo.
echo  ReleaseBuild/
echo    Play.bat
echo    Bin/
echo      KraftonEngine.exe + *.dll
echo    Shaders/
echo    Content/
echo    Settings/
echo.
pause
