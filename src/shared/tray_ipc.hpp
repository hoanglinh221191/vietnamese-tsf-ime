#pragma once

// One process writes the settings, and it is not the text service.
//
// A text service runs inside whatever application has focus, and some of those
// are packaged Store applications. A packaged process writing to HKCU does not
// write to HKCU: the package manager catches the write and puts a private copy
// in the package, which from then on shadows the real key for that application
// and for nothing else. Its reads see the copy too, so the service goes on
// editing a frozen snapshot and writing it back, and every setting saved from
// the config app is invisible to it for as long as the shadow exists.
//
// Measured on the machine where this was found: Notepad, which ships packaged,
// reported ConfigRevision 91800312 and 25 per-app rules while the real key held
// 36982296 and 17. The revision is a tick count, so the larger number is the
// later write - the packaged copy was written from inside the package, by the
// service itself. Windows Terminal is packaged the same way, which is why
// switching Neokey to English never reached it.
//
// So the service stops writing. It says what it wants remembered, and the tray
// - an ordinary process, outside any package - is the only thing that touches
// the registry.
//
// The tray answers requests from any process on the desktop, so what arrives
// here is data: a request names an application and a supported operation.
// QueryInputMode also reads through the tray: old package-private copies can
// shadow reads even after the service has stopped writing from inside MSIX.

#include <windows.h>

#include <cstdint>
#include <string_view>
#include <optional>
#include "config.hpp"

namespace vn_ime::tray_ipc {

// The tray's hidden window, which is how the service finds it.
inline constexpr const wchar_t* kWindowClass = L"NeokeyTrayWindowClass";
inline constexpr const wchar_t* kWindowTitle = L"NeokeyTray";

// WM_COPYDATA dwData. Any other value belongs to somebody else.
inline constexpr ULONG_PTR kConfigRequestId = 0x4E4B4359;  // 'NKCY'

inline constexpr uint32_t kProtocolVersion = 1;

// Sized from the limits in config.hpp, and checked against them there. A
// request that would not fit is refused rather than truncated: half a path is
// not a shorter path, it is a different one.
inline constexpr size_t kMaxProcessNameChars = 260;
inline constexpr size_t kMaxProcessPathChars = 512;

enum class RequestKind : uint32_t {
    // The service is leaving this application, and the app was switched off
    // while it was there.
    LearnAutomaticOff = 1,
    // The service is arriving, and an automatic rule may need restoring.
    RestoreAutomatic = 2,
    // The typist pressed the hotkey.
    ToggleMode = 3,
    QueryInputMode = 4,
};

struct ConfigRequest {
    uint32_t version = kProtocolVersion;
    uint32_t kind = 0;
    wchar_t process_name[kMaxProcessNameChars + 1] = {};
    wchar_t process_path[kMaxProcessPathChars + 1] = {};
};

// Whether this process is inside an application package, and so must not write
// settings at all. A path is allowed to be empty; a name is not.
bool IsPackagedProcess() noexcept;

// Fills `out`, or returns false when something does not fit or the kind is unknown.
bool BuildConfigRequest(RequestKind kind,
                        std::wstring_view process_name,
                        std::wstring_view process_path,
                        ConfigRequest& out) noexcept;

// Hands the request to the tray and waits briefly for it to be acted on.
// False when the tray is not running, did not answer, or refused - the caller
// then decides whether writing directly is safe, which it is only outside a
// package.
bool SendConfigRequest(const ConfigRequest& request) noexcept;

// A tagged scalar reply works across x86/x64 without cross-process pointers.
inline ULONG_PTR EncodeInputProfile(const ResolvedAppInputProfile& profile) {
    return 0x4E4B0100u | (profile.enabled ? 1u : 0u) |
        (profile.has_explicit_profile ? 2u : 0u) |
        (static_cast<ULONG_PTR>(profile.input_method) << 2);
}

inline std::optional<ResolvedAppInputProfile> DecodeInputProfile(ULONG_PTR value) {
    if ((value & ~ULONG_PTR(0xFu)) != 0x4E4B0100u) return std::nullopt;
    const auto method = static_cast<core::InputMethod>((value >> 2) & 3u);
    if (!IsValidAppInputMethod(method)) return std::nullopt;
    return ResolvedAppInputProfile{(value & 2u) != 0, (value & 1u) != 0, method};
}

std::optional<ResolvedAppInputProfile> QueryInputProfile(
    std::wstring_view process_name) noexcept;
std::optional<ResolvedAppInputProfile> QueryInputProfileFromWindow(
    HWND tray, std::wstring_view process_name) noexcept;

}  // namespace vn_ime::tray_ipc
