// Render thread for the GL calls (multicore): while it is active, every GL call made through the
// oyster::gl function pointers on another thread is recorded into a command stream and replayed
// in order on the render thread, which owns the context. The engine thread (story, animation,
// scene preparation) then runs in parallel with the driver work of the previous frame.
//
// Calls without results are recorded with copies of their data (uniform values, buffer and
// texture contents). Calls that return something or write through a pointer (glGet*, glCreate*,
// glCheckFramebufferStatus, glReadPixels, ...) wait until the render thread has replayed
// everything before them and executed the call. glGen* names come from pools refilled that way.
// GL calls made on the render thread itself (inside enqueue()d functions) go straight to the
// driver. The command order is the call order, so the GL results are identical.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>

namespace oyster::gl::threaded {

// `init` runs first on the new render thread (make the GL context current there - the calling
// thread must have released it), `exit` last (release it).
void start(std::function<void()> init, std::function<void()> exit);
// Replays everything, runs `exit`, joins the thread; GL calls are direct again afterwards.
void stop();
bool active();
bool onRenderThread();

// Runs `fn` on the render thread at this point of the command stream.
void enqueue(std::function<void()> fn);
// Hands the commands recorded so far to the render thread.
void flush();
// Ends a frame: flush, then wait until at most `maxAhead` earlier frames are still being replayed.
void endFrame(int maxAhead = 1);
// Waits until everything recorded so far has been replayed.
void finish();

struct Stats {
    double replayMs = 0;   // render thread busy replaying
    double waitMs = 0;     // engine thread blocked in endFrame/finish/sync calls
    uint64_t syncCalls = 0;
    uint64_t bytes = 0;    // command stream size
    uint64_t frames = 0;   // frames replayed
};
// Totals since start (read on the engine thread; values from the render thread lag by a frame).
Stats stats();

}  // namespace oyster::gl::threaded
