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

} // namespace vn_ime::core
