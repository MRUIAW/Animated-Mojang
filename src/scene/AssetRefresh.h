#pragma once
#include <utility>

namespace animated_mojang::scene {
// Asynchronous loaders can replace a placeholder rather than mutate it.
// Reacquire unresolved handles, with a bound on render-thread lookup frequency.
class AssetRefresh {
    double nextAttemptMs = 0;

public:
    template <class Handle, class Fetch, class Ready>
    bool update(Handle& handle, double timeMs, Fetch&& fetch, Ready&& ready) {
        if (ready(handle)) return true;
        if (timeMs < nextAttemptMs) return false;
        nextAttemptMs = timeMs + 100;
        handle        = std::forward<Fetch>(fetch)();
        return ready(handle);
    }
};
} // namespace animated_mojang::scene
