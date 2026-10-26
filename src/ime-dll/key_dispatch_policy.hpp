#pragma once

#include <windows.h>

namespace vn_ime {

// Whether an edit session asked of the host will run: the request was taken
// and the session did not fail. TF_S_ASYNC - queued to run as soon as the
// host allows an edit - is a success code, so a session queued that way still
// happens.
inline constexpr bool IsEditSessionAccepted(
    HRESULT request_hr, HRESULT session_hr) noexcept {
    return SUCCEEDED(request_hr) && SUCCEEDED(session_hr);
}

// Enter, Tab or an arrow pressed with a word composing is eaten and replayed
// by the session that commits the word. When the host refused that session
// both at once and for later, nothing replays the key and it was lost; it
// goes to the host as it is instead.
inline constexpr bool EatsKeyHeldForCommitReplay(
    bool eat, bool replay_after_commit, bool commit_accepted) noexcept {
    return eat && (!replay_after_commit || commit_accepted);
}

// A VNI digit with no word to put a mark on - nothing composing, and no word
// before the caret that it reconverts - types the digit and nothing else, so
// it goes to the app as typed. Put in a composition, a Flutter field (RustDesk)
// took each update of "1", "12", "123" as new text, and an ID came out longer
// than it was typed. Unless a shorthand begins with it: then it composes, so
// that the shorthand can still expand.
inline constexpr bool DigitGoesStraightToApp(
    bool vni, wchar_t ch, bool word_in_progress,
    bool shorthand_starts_with_it) noexcept {
    return vni && ch >= L'0' && ch <= L'9' && !word_in_progress &&
           !shorthand_starts_with_it;
}

// Whether a key replayed a moment after it was pressed - the Enter that
// SendSyntheticNativeKey holds back 60 ms for a page to catch up - still goes
// where it was pressed: the window in front is the one it was pressed in, and
// the keyboard focus, when it can be read, has not moved. Sent after a switch,
// that Enter sent the message or pressed the button of whatever had the focus
// by then. A key with no window to check goes as before.
inline bool DelayedKeyStillOnTarget(
    HWND target, HWND target_root, HWND foreground_root,
    HWND focus) noexcept {
    if (target == nullptr) {
        return true;
    }
    if (foreground_root != target_root) {
        return false;
    }
    return focus == nullptr || focus == target;
}

}  // namespace vn_ime
