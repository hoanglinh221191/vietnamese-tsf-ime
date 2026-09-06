#pragma once

// One physical keystroke reaches both TSF key sinks - OnTestKeyDown and then
// OnKeyDown - and some hosts call only one of them. Any state this service
// keeps by counting keys has to see each keystroke exactly once, or it drifts
// by a factor of two and every decision drawn from it is wrong.
//
// Neither sink's lParam nor a tick window can arbitrate that: the two sinks
// disagree on lParam, and a tick comparison comes apart whenever the pair
// straddles a GetTickCount64 boundary. Which sink already spoke for this
// keystroke is the only thing actually known, so that is what is recorded.
namespace vn_ime {

class KeySinkDeduplicator {
public:
    // True when this call should count the keystroke.
    bool ShouldObserve(unsigned virtual_key, bool from_test_sink) noexcept {
        if (from_test_sink) {
            test_sink_virtual_key_ = virtual_key;
            test_sink_pending_ = true;
            return true;
        }
        if (test_sink_pending_ && test_sink_virtual_key_ == virtual_key) {
            test_sink_pending_ = false;
            return false;
        }
        return true;
    }

private:
    unsigned test_sink_virtual_key_ = 0;
    bool test_sink_pending_ = false;
};

}  // namespace vn_ime
