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

:: Create build directories
if not exist build mkdir build
if not exist build\x64 mkdir build\x64
if not exist build\x86 mkdir build\x86

:: Clean up old files
del /q *.obj build\*.obj build\*.lib build\*.exp 2>nul

echo =========================================
echo Building 64-bit components...
echo =========================================
cmd.exe /c "call "!VCVARS!" amd64 && rc.exe /nologo /c65001 /fo build\resources.res /i src\config-app src\config-app\resources.rc"

cmd.exe /c "call "!VCVARS!" amd64 && cl.exe /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /LD /Isrc/shared /Isrc/ime-dll /Isrc/core /Fo"build\x64\\" /Febuild\neokey.dll src\ime-dll\dllmain.cpp src\ime-dll\ime_processor.cpp src\ime-dll\register.cpp src\core\rules.cpp src\core\engine.cpp src\core\speller.cpp src\shared\logger.cpp uuid.lib ole32.lib oleaut32.lib user32.lib advapi32.lib /link /def:src\ime-dll\neokey.def"

cmd.exe /c "call "!VCVARS!" amd64 && cl.exe /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /Isrc/shared /Isrc/ime-dll /Isrc/core /Fo"build\x64\\" /Febuild\neokey_config.exe src\config-app\main.cpp src\shared\logger.cpp build\resources.res /link /subsystem:windows comctl32.lib advapi32.lib user32.lib comdlg32.lib"

cmd.exe /c "call "!VCVARS!" amd64 && cl.exe /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /Isrc/core /Isrc/shared /Fo"build\x64\\" /Febuild\core_tests.exe tests\core_tests.cpp src\core\rules.cpp src\core\engine.cpp src\core\speller.cpp src\shared\logger.cpp advapi32.lib user32.lib"

echo =========================================
echo Building 32-bit components...
echo =========================================
cmd.exe /c "call "!VCVARS!" x86 && cl.exe /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /LD /Isrc/shared /Isrc/ime-dll /Isrc/core /Fo"build\x86\\" /Febuild\neokey32.dll src\ime-dll\dllmain.cpp src\ime-dll\ime_processor.cpp src\ime-dll\register.cpp src\core\rules.cpp src\core\engine.cpp src\core\speller.cpp src\shared\logger.cpp uuid.lib ole32.lib oleaut32.lib user32.lib advapi32.lib /link /def:src\ime-dll\neokey.def"

:: Clean up temp obj files in root if any
del /q *.obj 2>nul

echo Build complete!
