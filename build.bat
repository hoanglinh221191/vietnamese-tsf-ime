@echo off
setlocal enabledelayedexpansion

:: Locate VS installation path
for /f "usebackq tokens=*" %%i in (`"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do (
    set "VS_PATH=%%i"
)

if not defined VS_PATH (
    echo Error: Visual Studio installation not found.
    exit /b 1
)

set "VCVARS=!VS_PATH!\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "!VCVARS!" (
    echo Error: vcvarsall.bat not found at !VCVARS!
    exit /b 1
)

if not defined OUT_DIR set "OUT_DIR=build"
set "OBJ_X64=!OUT_DIR!\x64"
set "OBJ_X86=!OUT_DIR!\x86"

:: Create build directories
if not exist "!OUT_DIR!" mkdir "!OUT_DIR!"
if not exist "!OBJ_X64!" mkdir "!OBJ_X64!"
if not exist "!OBJ_X86!" mkdir "!OBJ_X86!"

:: Clean up old files
del /q *.obj "!OUT_DIR!\*.obj" "!OUT_DIR!\*.lib" "!OUT_DIR!\*.exp" 2>nul

echo =========================================
echo Building 64-bit components...
echo =========================================
cmd.exe /c "call "!VCVARS!" amd64 && rc.exe /nologo /c65001 /fo !OUT_DIR!\resources.res /i src\config-app src\config-app\resources.rc"
if errorlevel 1 exit /b 1

cmd.exe /c "call "!VCVARS!" amd64 && cl.exe /nologo /std:c++latest /utf-8 /EHsc /MT /O2 /guard:cf /LD /Isrc/shared /Isrc/ime-dll /Isrc/core /Fo"!OBJ_X64!\\" /Fe!OUT_DIR!\neokey.dll src\ime-dll\dllmain.cpp src\ime-dll\ime_processor.cpp src\ime-dll\register.cpp src\core\rules.cpp src\core\engine.cpp src\core\speller.cpp src\shared\logger.cpp uuid.lib ole32.lib oleaut32.lib user32.lib advapi32.lib comctl32.lib /link /def:src\ime-dll\neokey.def /guard:cf /DYNAMICBASE /NXCOMPAT"
if errorlevel 1 exit /b 1

cmd.exe /c "call "!VCVARS!" amd64 && cl.exe /nologo /std:c++latest /utf-8 /EHsc /MT /O2 /guard:cf /Isrc/shared /Isrc/ime-dll /Isrc/core /Fo"!OBJ_X64!\\" /Fe!OUT_DIR!\neokey_config.exe src\config-app\main.cpp src\shared\logger.cpp !OUT_DIR!\resources.res /link /subsystem:windows comctl32.lib advapi32.lib user32.lib comdlg32.lib gdi32.lib shell32.lib /guard:cf /DYNAMICBASE /NXCOMPAT"
if errorlevel 1 exit /b 1

cmd.exe /c "call "!VCVARS!" amd64 && cl.exe /nologo /std:c++latest /utf-8 /EHsc /MT /O2 /guard:cf /Isrc/core /Isrc/shared /Isrc/ime-dll /Fo"!OBJ_X64!\\" /Fe!OUT_DIR!\core_tests.exe tests\core_tests.cpp src\core\rules.cpp src\core\engine.cpp src\core\speller.cpp src\shared\logger.cpp advapi32.lib user32.lib /link /guard:cf /DYNAMICBASE /NXCOMPAT"
if errorlevel 1 exit /b 1

echo =========================================
echo Building 32-bit components...
echo =========================================
cmd.exe /c "call "!VCVARS!" x86 && cl.exe /nologo /std:c++latest /utf-8 /EHsc /MT /O2 /guard:cf /LD /Isrc/shared /Isrc/ime-dll /Isrc/core /Fo"!OBJ_X86!\\" /Fe!OUT_DIR!\neokey32.dll src\ime-dll\dllmain.cpp src\ime-dll\ime_processor.cpp src\ime-dll\register.cpp src\core\rules.cpp src\core\engine.cpp src\core\speller.cpp src\shared\logger.cpp uuid.lib ole32.lib oleaut32.lib user32.lib advapi32.lib comctl32.lib /link /def:src\ime-dll\neokey.def /guard:cf /DYNAMICBASE /NXCOMPAT"
if errorlevel 1 exit /b 1

:: Clean up temp obj files in root if any
del /q *.obj 2>nul

echo Build complete!
exit /b 0
