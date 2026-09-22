#pragma once

// One process writes the settings, and it is not the text service.
//
// A text service runs inside whatever application has focus, so it writes HKCU
// as that application, under whatever the host does to the registry. That
// turned out to matter: settings saved in the config app did not reach some
// applications at all - Windows Terminal was the one reported and reproduced -
// while the same save reached others on the next keystroke. A service that both
// reads and writes can also go on editing a stale copy and saving it back, so
// the wrong value survives being corrected.
//
// So the service stops writing. It says what it wants remembered, and the tray
// - one ordinary process, in one place - is the only thing that touches the
// registry. Reads go the same way, through QueryInputMode, so a host that
// answers registry reads oddly cannot make the service disagree with the tray
// about what the settings are.
//
// Why those hosts behaved differently has not been established. An earlier
// version of this comment blamed MSIX package registry virtualization and
// quoted figures for it; the figures came from a sandboxed diagnostic shell
// rather than from the shipping processes, and the explanation did not survive
// being checked. The design stands on the symptom, which was real and is fixed,
// not on that account of it.
//
// The tray answers requests from any process on the desktop, so what arrives
// here is data: a request names an application and a supported operation.

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
