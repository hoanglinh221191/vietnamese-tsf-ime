VS_PATH = $(shell "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath)
VCVARS = "$(VS_PATH)\VC\Auxiliary\Build\vcvarsall.bat"

OUT_DIR = build
TARGET_X64 = $(OUT_DIR)/neokey.dll
TARGET_X86 = $(OUT_DIR)/neokey32.dll
TARGET_ARM64 = $(OUT_DIR)/neokey_arm64.dll
CONFIG_TARGET = $(OUT_DIR)/neokey_config.exe
CONFIG_TARGET_ARM64 = $(OUT_DIR)/neokey_config_arm64.exe
TEST_TARGET = $(OUT_DIR)/core_tests.exe
TEST_TARGET_X86 = $(OUT_DIR)/core_tests32.exe
TEST_TARGET_ARM64 = $(OUT_DIR)/core_tests_arm64.exe

DLL_SOURCES = src/ime-dll/dllmain.cpp \
              src/ime-dll/ime_processor.cpp \
              src/ime-dll/register.cpp \
              src/ime-dll/fake_backspace_handler.cpp \
              src/core/rules.cpp \
              src/core/engine.cpp \
              src/core/speller.cpp \
              src/shared/logger.cpp

CONFIG_SOURCES = src/config-app/main.cpp \
                 src/shared/logger.cpp

TEST_SOURCES = tests/core_tests.cpp \
               src/ime-dll/fake_backspace_handler.cpp \
               src/core/rules.cpp \
               src/core/engine.cpp \
               src/core/speller.cpp \
               src/shared/logger.cpp

DLL_LIBS = uuid.lib ole32.lib oleaut32.lib user32.lib advapi32.lib comctl32.lib
CONFIG_LIBS = comctl32.lib advapi32.lib user32.lib comdlg32.lib gdi32.lib shell32.lib dwmapi.lib uxtheme.lib
HARDEN_FLAGS = /guard:cf
HARDEN_LINK_FLAGS = /guard:cf /DYNAMICBASE /NXCOMPAT
OBJ_DLL_X64 = $(OUT_DIR)/make-dll-x64
OBJ_DLL_X86 = $(OUT_DIR)/make-dll-x86
OBJ_DLL_ARM64 = $(OUT_DIR)/make-dll-arm64
OBJ_CONFIG_X64 = $(OUT_DIR)/make-config-x64
OBJ_CONFIG_ARM64 = $(OUT_DIR)/make-config-arm64
OBJ_TEST_X64 = $(OUT_DIR)/make-test-x64
OBJ_TEST_X86 = $(OUT_DIR)/make-test-x86
OBJ_TEST_ARM64 = $(OUT_DIR)/make-test-arm64

all: $(TARGET_X64) $(TARGET_X86) $(CONFIG_TARGET)

arm64: $(TARGET_ARM64) $(CONFIG_TARGET_ARM64) $(TEST_TARGET_ARM64)

$(TARGET_X64): $(DLL_SOURCES)
	@if not exist "$(OUT_DIR)" mkdir "$(OUT_DIR)"
	@if not exist "$(OBJ_DLL_X64)" mkdir "$(OBJ_DLL_X64)"
	cmd.exe /c "call $(VCVARS) amd64 && cl.exe /nologo /std:c++latest /utf-8 /EHsc /MT /O2 $(HARDEN_FLAGS) /LD /Isrc/shared /Isrc/ime-dll /Isrc/core /Fo$(OBJ_DLL_X64)\\ /Fe$(TARGET_X64) $(DLL_SOURCES) $(DLL_LIBS) /link /def:src/ime-dll/neokey.def $(HARDEN_LINK_FLAGS)"

$(TARGET_X86): $(DLL_SOURCES)
	@if not exist "$(OUT_DIR)" mkdir "$(OUT_DIR)"
	@if not exist "$(OBJ_DLL_X86)" mkdir "$(OBJ_DLL_X86)"
	cmd.exe /c "call $(VCVARS) x86 && cl.exe /nologo /std:c++latest /utf-8 /EHsc /MT /O2 $(HARDEN_FLAGS) /LD /Isrc/shared /Isrc/ime-dll /Isrc/core /Fo$(OBJ_DLL_X86)\\ /Fe$(TARGET_X86) $(DLL_SOURCES) $(DLL_LIBS) /link /def:src/ime-dll/neokey.def $(HARDEN_LINK_FLAGS)"

$(TARGET_ARM64): $(DLL_SOURCES)
	@if not exist "$(OUT_DIR)" mkdir "$(OUT_DIR)"
	@if not exist "$(OBJ_DLL_ARM64)" mkdir "$(OBJ_DLL_ARM64)"
	cmd.exe /c "call $(VCVARS) amd64_arm64 && cl.exe /nologo /std:c++latest /utf-8 /EHsc /MT /O2 $(HARDEN_FLAGS) /LD /Isrc/shared /Isrc/ime-dll /Isrc/core /Fo$(OBJ_DLL_ARM64)\\ /Fe$(TARGET_ARM64) $(DLL_SOURCES) $(DLL_LIBS) /link /def:src/ime-dll/neokey.def $(HARDEN_LINK_FLAGS)"

$(CONFIG_TARGET): $(CONFIG_SOURCES) $(OUT_DIR)/resources.res
	@if not exist "$(OUT_DIR)" mkdir "$(OUT_DIR)"
	@if not exist "$(OBJ_CONFIG_X64)" mkdir "$(OBJ_CONFIG_X64)"
	cmd.exe /c "call $(VCVARS) amd64 && cl.exe /nologo /std:c++latest /utf-8 /EHsc /MT /O2 $(HARDEN_FLAGS) /Isrc/shared /Isrc/ime-dll /Isrc/core /Fo$(OBJ_CONFIG_X64)\\ /Fe$(CONFIG_TARGET) $(CONFIG_SOURCES) $(OUT_DIR)/resources.res /link /subsystem:windows $(CONFIG_LIBS) $(HARDEN_LINK_FLAGS)"

$(CONFIG_TARGET_ARM64): $(CONFIG_SOURCES) $(OUT_DIR)/resources_arm64.res
	@if not exist "$(OUT_DIR)" mkdir "$(OUT_DIR)"
	@if not exist "$(OBJ_CONFIG_ARM64)" mkdir "$(OBJ_CONFIG_ARM64)"
	cmd.exe /c "call $(VCVARS) amd64_arm64 && cl.exe /nologo /std:c++latest /utf-8 /EHsc /MT /O2 $(HARDEN_FLAGS) /Isrc/shared /Isrc/ime-dll /Isrc/core /Fo$(OBJ_CONFIG_ARM64)\\ /Fe$(CONFIG_TARGET_ARM64) $(CONFIG_SOURCES) $(OUT_DIR)/resources_arm64.res /link /subsystem:windows $(CONFIG_LIBS) $(HARDEN_LINK_FLAGS)"

$(OUT_DIR)/resources.res: src/config-app/resources.rc src/config-app/resources.h src/config-app/manifest.xml
	@if not exist "$(OUT_DIR)" mkdir "$(OUT_DIR)"
	cmd.exe /c "call $(VCVARS) amd64 && rc.exe /nologo /c65001 /fo $(OUT_DIR)/resources.res /i src/config-app src/config-app/resources.rc"

$(OUT_DIR)/resources_arm64.res: src/config-app/resources.rc src/config-app/resources.h src/config-app/manifest.xml
	@if not exist "$(OUT_DIR)" mkdir "$(OUT_DIR)"
	cmd.exe /c "call $(VCVARS) amd64_arm64 && rc.exe /nologo /c65001 /fo $(OUT_DIR)/resources_arm64.res /i src/config-app src/config-app/resources.rc"

tests: $(TEST_TARGET) $(TEST_TARGET_X86)

$(TEST_TARGET): $(TEST_SOURCES)
	@if not exist "$(OUT_DIR)" mkdir "$(OUT_DIR)"
	@if not exist "$(OBJ_TEST_X64)" mkdir "$(OBJ_TEST_X64)"
	cmd.exe /c "call $(VCVARS) amd64 && cl.exe /nologo /std:c++latest /utf-8 /EHsc /MT /O2 $(HARDEN_FLAGS) /Isrc/core /Isrc/shared /Isrc/ime-dll /Fo$(OBJ_TEST_X64)\\ /Fe$(TEST_TARGET) $(TEST_SOURCES) advapi32.lib user32.lib /link $(HARDEN_LINK_FLAGS)"

$(TEST_TARGET_X86): $(TEST_SOURCES)
	@if not exist "$(OUT_DIR)" mkdir "$(OUT_DIR)"
	@if not exist "$(OBJ_TEST_X86)" mkdir "$(OBJ_TEST_X86)"
	cmd.exe /c "call $(VCVARS) x86 && cl.exe /nologo /std:c++latest /utf-8 /EHsc /MT /O2 $(HARDEN_FLAGS) /Isrc/core /Isrc/shared /Isrc/ime-dll /Fo$(OBJ_TEST_X86)\\ /Fe$(TEST_TARGET_X86) $(TEST_SOURCES) advapi32.lib user32.lib /link $(HARDEN_LINK_FLAGS)"

$(TEST_TARGET_ARM64): $(TEST_SOURCES)
	@if not exist "$(OUT_DIR)" mkdir "$(OUT_DIR)"
	@if not exist "$(OBJ_TEST_ARM64)" mkdir "$(OBJ_TEST_ARM64)"
	cmd.exe /c "call $(VCVARS) amd64_arm64 && cl.exe /nologo /std:c++latest /utf-8 /EHsc /MT /O2 $(HARDEN_FLAGS) /Isrc/core /Isrc/shared /Isrc/ime-dll /Fo$(OBJ_TEST_ARM64)\\ /Fe$(TEST_TARGET_ARM64) $(TEST_SOURCES) advapi32.lib user32.lib /link $(HARDEN_LINK_FLAGS)"

clean:
	@if exist $(OUT_DIR) rmdir /s /q $(OUT_DIR)
	@del /q *.obj *.lib *.exp *.res *.o 2>nul || exit 0

.PHONY: all arm64 clean tests
