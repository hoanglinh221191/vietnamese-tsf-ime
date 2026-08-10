#pragma once
#include <cstdint>

namespace vn_ime::core {

enum class InputMethod {
    Telex,
    SimpleTelex,
    VNI
};

enum class ToneMark {
    None,
    Sacute, // Sắc
    Grave,  // Huyền
    Hook,   // Hỏi
    Tilde,  // Ngã
    Dot     // Nặng
};

enum class CorrectionLevel : uint8_t {
    Off = 0,
    Normal = 1,
    Advanced = 2,
    Experimental = 3,
};

enum class EnglishProtectionLevel : uint8_t {
    Off = 0,
    Balanced = 1,
    EnglishFirst = 2,
};

} // namespace vn_ime::core
