VS_PATH = $(shell "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath)
VCVARS = "$(VS_PATH)\VC\Auxiliary\Build\vcvarsall.bat"

OUT_DIR = build
TARGET_X64 = $(OUT_DIR)/neokey.dll
TARGET_X86 = $(OUT_DIR)/neokey32.dll
CONFIG_TARGET = $(OUT_DIR)/neokey_config.exe
TEST_TARGET = $(OUT_DIR)/core_tests.exe

DLL_SOURCES = src/ime-dll/dllmain.cpp \
              src/ime-dll/ime_processor.cpp \
              src/ime-dll/register.cpp \
              src/core/rules.cpp \
              src/core/engine.cpp \
              src/core/speller.cpp \
              src/shared/logger.cpp

CONFIG_SOURCES = src/config-app/main.cpp \
                 src/shared/logger.cpp

TEST_SOURCES = tests/core_tests.cpp \
               src/core/rules.cpp \
               src/core/engine.cpp \
               src/core/speller.cpp \
               src/shared/logger.cpp

DLL_LIBS = uuid.lib ole32.lib oleaut32.lib user32.lib advapi32.lib
CONFIG_LIBS = comctl32.lib advapi32.lib user32.lib comdlg32.lib

all: $(TARGET_X64) $(TARGET_X86) $(CONFIG_TARGET)

$(TARGET_X64): $(DLL_SOURCES)
	@if not exist $(OUT_DIR) mkdir $(OUT_DIR)
	cmd.exe /c "call $(VCVARS) amd64 && cl.exe /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /LD /Isrc/shared /Isrc/ime-dll /Isrc/core /Fe$(TARGET_X64) $(DLL_SOURCES) $(DLL_LIBS) /link /def:src/ime-dll/neokey.def"

$(TARGET_X86): $(DLL_SOURCES)
	@if not exist $(OUT_DIR) mkdir $(OUT_DIR)
	cmd.exe /c "call $(VCVARS) x86 && cl.exe /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /LD /Isrc/shared /Isrc/ime-dll /Isrc/core /Fe$(TARGET_X86) $(DLL_SOURCES) $(DLL_LIBS) /link /def:src/ime-dll/neokey.def"

$(CONFIG_TARGET): $(CONFIG_SOURCES) $(OUT_DIR)/resources.res
	@if not exist $(OUT_DIR) mkdir $(OUT_DIR)
	cmd.exe /c "call $(VCVARS) amd64 && cl.exe /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /Isrc/shared /Isrc/ime-dll /Isrc/core /Fe$(CONFIG_TARGET) $(CONFIG_SOURCES) $(OUT_DIR)/resources.res /link /subsystem:windows $(CONFIG_LIBS)"

$(OUT_DIR)/resources.res: src/config-app/resources.rc src/config-app/resources.h src/config-app/manifest.xml
	@if not exist $(OUT_DIR) mkdir $(OUT_DIR)
	cmd.exe /c "call $(VCVARS) amd64 && rc.exe /nologo /c65001 /fo $(OUT_DIR)/resources.res /i src/config-app src/config-app/resources.rc"

tests: $(TEST_TARGET)

$(TEST_TARGET): $(TEST_SOURCES)
	@if not exist $(OUT_DIR) mkdir $(OUT_DIR)
	cmd.exe /c "call $(VCVARS) amd64 && cl.exe /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /Isrc/core /Isrc/shared /Fe$(TEST_TARGET) $(TEST_SOURCES) advapi32.lib user32.lib"

clean:
	@if exist $(OUT_DIR) rmdir /s /q $(OUT_DIR)
	@del /q *.obj *.lib *.exp *.res *.o 2>nul || exit 0

.PHONY: all clean tests
