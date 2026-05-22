CXX = C:/msys64/ucrt64/bin/clang++.exe
RC = C:/msys64/ucrt64/bin/windres.exe
CXXFLAGS = -std=c++23 -O3 -fno-exceptions -Wall -Wextra -Isrc/shared -Isrc/ime-dll -Isrc/core -static
DLL_FLAGS = -shared
LIBS = -luuid -lole32 -loleaut32 -luser32 -ladvapi32

OUT_DIR = build
TARGET = $(OUT_DIR)/vn_tsf_ime.dll
CONFIG_TARGET = $(OUT_DIR)/vn_tsf_ime_config.exe

SOURCES = src/ime-dll/dllmain.cpp \
          src/ime-dll/ime_processor.cpp \
          src/ime-dll/register.cpp \
          src/core/rules.cpp \
          src/core/engine.cpp \
          src/core/speller.cpp \
          src/shared/logger.cpp

CONFIG_SOURCES = src/config-app/main.cpp
CONFIG_RESOURCES = src/config-app/resources.rc
CONFIG_RES_OBJ = $(OUT_DIR)/resources.o

TEST_TARGET = $(OUT_DIR)/core_tests.exe
TEST_SOURCES = tests/core_tests.cpp \
               src/core/rules.cpp \
               src/core/engine.cpp \
               src/core/speller.cpp

all: $(TARGET) $(CONFIG_TARGET)

$(TARGET): $(SOURCES)
	@if not exist $(OUT_DIR) mkdir $(OUT_DIR)
	$(CXX) $(CXXFLAGS) $(DLL_FLAGS) -o $(TARGET) $(SOURCES) $(LIBS)

$(CONFIG_TARGET): $(CONFIG_SOURCES) $(CONFIG_RES_OBJ)
	@if not exist $(OUT_DIR) mkdir $(OUT_DIR)
	$(CXX) $(CXXFLAGS) -mwindows -o $(CONFIG_TARGET) $(CONFIG_SOURCES) $(CONFIG_RES_OBJ) -lcomctl32 -ladvapi32 -luser32

$(CONFIG_RES_OBJ): $(CONFIG_RESOURCES) src/config-app/manifest.xml
	@if not exist $(OUT_DIR) mkdir $(OUT_DIR)
	$(RC) -Isrc/config-app -i $(CONFIG_RESOURCES) -o $(CONFIG_RES_OBJ)

tests: $(TEST_TARGET)

$(TEST_TARGET): $(TEST_SOURCES)
	@if not exist $(OUT_DIR) mkdir $(OUT_DIR)
	$(CXX) -std=c++23 -O3 -fno-exceptions -Wall -Wextra -Isrc/core -static -o $(TEST_TARGET) $(TEST_SOURCES)

clean:
	@if exist $(OUT_DIR) rmdir /s /q $(OUT_DIR)

.PHONY: all clean tests
