#pragma once

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

} // namespace vn_ime::core
