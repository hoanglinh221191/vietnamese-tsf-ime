CXX = C:/msys64/ucrt64/bin/clang++.exe
CXXFLAGS = -std=c++23 -O3 -fno-exceptions -Wall -Wextra -Isrc/shared -Isrc/ime-dll -Isrc/core -shared -static
LIBS = -luuid -lole32 -loleaut32 -luser32 -ladvapi32

OUT_DIR = build
TARGET = $(OUT_DIR)/vn_tsf_ime.dll

SOURCES = src/ime-dll/dllmain.cpp \
          src/ime-dll/ime_processor.cpp \
          src/ime-dll/register.cpp \
          src/core/rules.cpp \
          src/core/engine.cpp \
          src/core/speller.cpp \
          src/shared/logger.cpp

TEST_TARGET = $(OUT_DIR)/core_tests.exe
TEST_SOURCES = tests/core_tests.cpp \
               src/core/rules.cpp \
               src/core/engine.cpp \
               src/core/speller.cpp

all: $(TARGET)

$(TARGET): $(SOURCES)
	@if not exist $(OUT_DIR) mkdir $(OUT_DIR)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCES) $(LIBS)

tests: $(TEST_TARGET)

$(TEST_TARGET): $(TEST_SOURCES)
	@if not exist $(OUT_DIR) mkdir $(OUT_DIR)
	$(CXX) -std=c++23 -O3 -fno-exceptions -Wall -Wextra -Isrc/core -static -o $(TEST_TARGET) $(TEST_SOURCES)

clean:
	@if exist $(OUT_DIR) rmdir /s /q $(OUT_DIR)

.PHONY: all clean tests
