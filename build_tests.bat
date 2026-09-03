@echo off
setlocal enabledelayedexpansion
for /f "usebackq tokens=*" %%i in (`"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do (
    set "VS_PATH=%%i"
)
if not defined VS_PATH (
    echo Error: VS not found.
    exit /b 1
)
set "VCVARS=!VS_PATH!\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "!VCVARS!" (
    echo Error: VCVARS not found.
    exit /b 1
)
if not defined OUT_DIR set "OUT_DIR=build"
set "OBJ_X64=!OUT_DIR!\x64"
set "OBJ_X86=!OUT_DIR!\x86"
if not exist "!OUT_DIR!" mkdir "!OUT_DIR!"
if not exist "!OBJ_X64!" mkdir "!OBJ_X64!"
if not exist "!OBJ_X86!" mkdir "!OBJ_X86!"
cmd.exe /c "call "!VCVARS!" amd64 && cl.exe /nologo /std:c++latest /utf-8 /EHsc /MT /O2 /guard:cf /Isrc/core /Isrc/shared /Isrc/ime-dll /Fo"!OBJ_X64!\\" /Fe!OUT_DIR!\core_tests.exe tests\core_tests.cpp src\ime-dll\fake_backspace_handler.cpp src\core\rules.cpp src\core\engine.cpp src\core\speller.cpp src\core\fuzzy_input.cpp src\shared\logger.cpp advapi32.lib user32.lib /link /guard:cf /DYNAMICBASE /NXCOMPAT"
if errorlevel 1 exit /b 1
cmd.exe /c "call "!VCVARS!" x86 && cl.exe /nologo /std:c++latest /utf-8 /EHsc /MT /O2 /guard:cf /Isrc/core /Isrc/shared /Isrc/ime-dll /Fo"!OBJ_X86!\\" /Fe!OUT_DIR!\core_tests32.exe tests\core_tests.cpp src\ime-dll\fake_backspace_handler.cpp src\core\rules.cpp src\core\engine.cpp src\core\speller.cpp src\core\fuzzy_input.cpp src\shared\logger.cpp advapi32.lib user32.lib /link /guard:cf /DYNAMICBASE /NXCOMPAT"
if errorlevel 1 exit /b 1
