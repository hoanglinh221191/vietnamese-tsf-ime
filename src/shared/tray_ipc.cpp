#include "tray_ipc.hpp"

#include "config.hpp"

#include <algorithm>

namespace vn_ime::tray_ipc {

static_assert(kMaxProcessNameChars >= MAX_APP_INPUT_PROFILE_PROCESS_NAME_CHARS,
              "a name the settings accept must fit in a request");
static_assert(kMaxProcessPathChars >= MAX_APP_PROFILE_PATH_CHARS,
              "a path the settings accept must fit in a request");

namespace {

// Copies with room for the terminator, or refuses. std::copy rather than
// wcsncpy_s, which truncates and calls that success.
bool CopyField(std::wstring_view source, wchar_t* destination,
               size_t capacity_chars) noexcept {
    if (source.length() >= capacity_chars) {
        return false;
    }
    std::copy(source.begin(), source.end(), destination);
    destination[source.length()] = L'\0';
    return true;
}

}  // namespace

bool IsPackagedProcess() noexcept {
    // Resolved rather than linked: the answer is the same for the life of the
    // process, and an older Windows without the entry point simply has no
    // packages to be inside.
    using GetCurrentPackageFullNameFn =
        LONG(WINAPI*)(UINT32*, PWSTR);
    static const bool packaged = [] {
        const HMODULE kernel = ::GetModuleHandleW(L"kernel32.dll");
        if (!kernel) {
            return false;
        }
        const auto get_name = reinterpret_cast<GetCurrentPackageFullNameFn>(
            reinterpret_cast<void*>(
                ::GetProcAddress(kernel, "GetCurrentPackageFullName")));
        if (!get_name) {
            return false;
        }
        UINT32 length = 0;
        const LONG status = get_name(&length, nullptr);
        // ERROR_INSUFFICIENT_BUFFER means there is a name to return, so there
        // is a package. APPMODEL_ERROR_NO_PACKAGE means there is not.
        return status != APPMODEL_ERROR_NO_PACKAGE;
    }();
    return packaged;
}

bool BuildConfigRequest(RequestKind kind,
                        std::wstring_view process_name,
                        std::wstring_view process_path,
                        ConfigRequest& out) noexcept {
    if (kind != RequestKind::LearnAutomaticOff &&
        kind != RequestKind::RestoreAutomatic &&
        kind != RequestKind::ToggleMode &&
        kind != RequestKind::QueryInputMode) {
        return false;
    }
    if (process_name.empty()) {
        return false;
    }
    out = ConfigRequest{};
    out.version = kProtocolVersion;
    out.kind = static_cast<uint32_t>(kind);
    return CopyField(process_name, out.process_name, kMaxProcessNameChars + 1) &&
           CopyField(process_path, out.process_path, kMaxProcessPathChars + 1);
}

bool SendConfigRequest(const ConfigRequest& request) noexcept {
    const HWND tray = ::FindWindowW(kWindowClass, kWindowTitle);
    if (!tray) {
        return false;
    }

    COPYDATASTRUCT payload{};
    payload.dwData = kConfigRequestId;
    payload.cbData = static_cast<DWORD>(sizeof(ConfigRequest));
    payload.lpData = const_cast<ConfigRequest*>(&request);

    // A timeout, because this runs on the thread that is handling a keystroke
    // and the tray may be busy with a dialog. Losing the rule is better than
    // stalling the typist; the next activation asks again.
    DWORD_PTR answer = 0;
    const LRESULT sent = ::SendMessageTimeoutW(
        tray, WM_COPYDATA, reinterpret_cast<WPARAM>(nullptr),
        reinterpret_cast<LPARAM>(&payload),
        SMTO_ABORTIFHUNG | SMTO_NORMAL, 2000, &answer);
    return sent != 0 && answer != 0;
}

std::optional<ResolvedAppInputProfile> QueryInputProfile(
    std::wstring_view process_name) noexcept {
    return QueryInputProfileFromWindow(
        ::FindWindowW(kWindowClass, kWindowTitle), process_name);
}

std::optional<ResolvedAppInputProfile> QueryInputProfileFromWindow(
    HWND tray, std::wstring_view process_name) noexcept {
    ConfigRequest request;
    if (!BuildConfigRequest(RequestKind::QueryInputMode, process_name, L"", request))
        return std::nullopt;
    if (!tray) return std::nullopt;
    COPYDATASTRUCT payload{kConfigRequestId, sizeof(request), &request};
    DWORD_PTR answer = 0;
    // This is on the input path. Never wait seconds for a busy tray, and do
    // not dispatch nested input callbacks while waiting for its answer.
    if (!::SendMessageTimeoutW(tray, WM_COPYDATA, 0,
            reinterpret_cast<LPARAM>(&payload),
            SMTO_ABORTIFHUNG | SMTO_BLOCK, 30, &answer)) return std::nullopt;
    return DecodeInputProfile(answer);
}

}  // namespace vn_ime::tray_ipc
