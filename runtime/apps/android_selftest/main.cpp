// oyster_selftest: runs the story on an Android device from `adb shell`, without an app window,
// OpenXR session or anyone wearing the headset. EGL pbuffer context, offscreen stereo rendering
// at the Quest 3's eye size and field of view with a still, seated head, fixed time step.
// Prints CPU+GPU time per frame (glFinish) and texture loading to stdout.
//
// Usage (device): oyster_selftest <pearl data> [frames=3000] [hz=72] [eyeW eyeH [msaa=2]]
//   e.g. adb push oyster_selftest /data/local/tmp && adb shell /data/local/tmp/oyster_selftest /sdcard/Oyster/pearl 6000
// GL runs on a render thread like in the app (render/gl_thread.h; OYSTER_THREADED=0: single
// thread, glFinish after every frame). Threaded, the GPU may run two frames behind (fences).
#include <EGL/egl.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <string>
#include <vector>

#include "core/pkgfs.h"
#include "render/gl.h"
#include "render/gl_thread.h"
#include "render/renderer.h"
#include "story/engine.h"

using namespace oyster;
using namespace oyster::gl;

namespace {

void* glProc(const char* name) {
    static void* lib = dlopen("libGLESv3.so", RTLD_NOW | RTLD_LOCAL);
    void* p = lib ? dlsym(lib, name) : nullptr;
    if (!p) p = reinterpret_cast<void*>(eglGetProcAddress(name));
    return p;
}

double nowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: oyster_selftest <pearl data> [frames] [hz] [eyeW eyeH]\n");
        return 2;
    }
    std::string root = argv[1];
    int frames = argc > 2 ? std::atoi(argv[2]) : 3000;
    double hz = argc > 3 ? std::atof(argv[3]) : 72.0;
    int eyeW = argc > 5 ? std::atoi(argv[4]) : 1680, eyeH = argc > 5 ? std::atoi(argv[5]) : 1760;
    int msaa = argc > 6 ? std::atoi(argv[6]) : 2;
    setvbuf(stdout, nullptr, _IOLBF, 0);

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLConfig cfg = nullptr;
    EGLint n = 0;
    const EGLint ca[] = {EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_SURFACE_TYPE,
                         EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, 0x40, EGL_NONE};
    const EGLint xa[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    const EGLint pa[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, nullptr, nullptr) || !eglChooseConfig(dpy, ca, &cfg, 1, &n) || n != 1) {
        std::fprintf(stderr, "EGL setup failed 0x%x\n", eglGetError());
        return 1;
    }
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, xa);
    EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pa);
    if (ctx == EGL_NO_CONTEXT || surf == EGL_NO_SURFACE || !eglMakeCurrent(dpy, surf, surf, ctx)) {
        std::fprintf(stderr, "EGL context failed 0x%x\n", eglGetError());
        return 1;
    }
    if (const char* missing = gl::load(glProc)) {
        std::fprintf(stderr, "missing GL function %s\n", missing);
        return 1;
    }
    std::printf("GL %s / %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    const char* thr = std::getenv("OYSTER_THREADED");
    bool threaded = !thr || std::string(thr) != "0";
    if (threaded) {
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        gl::threaded::start([=] { eglMakeCurrent(dpy, surf, surf, ctx); },
                            [=] { eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT); });
        std::printf("render thread on\n");
    }
    try {
        double t0 = nowMs();
        PackageFS fs(root);
        std::printf("data indexed in %.0f ms\n", nowMs() - t0);
        story::EngineOptions opt;
        opt.width = eyeW;
        opt.height = eyeH;
        opt.msaa = msaa;
        opt.precompileShaders = true;
        opt.shaderCacheDir = "shadercache";  // relative to the working directory
        opt.prefetchHints = root + "/../prefetch_hints.txt";  // shared with the app (next to the data)
        mkdir("shadercache", 0755);
        story::Engine engine(fs, opt);
        story::HmdState& h = engine.hmd();
        h.active = true;
        h.width = eyeW;
        h.height = eyeH;
        h.position = Vec3(0, 1.2f, 0);
        h.eye[0] = {-1.13f, 0.84f, 0.90f, -1.13f};
        h.eye[1] = {-0.84f, 1.13f, 0.90f, -1.13f};
        engine.renderer().setTextureBudget(size_t(1024) << 20);
        engine.time().fixedStepMs = 1000.0 / hz;
        t0 = nowMs();
        if (!engine.boot()) throw std::runtime_error("story boot failed: " + engine.lua().lastError());
        std::printf("story booted in %.0f ms\n", nowMs() - t0);
        Renderer& rr = engine.renderer();
        double budget = 1000.0 / hz, sum = 0, mx = 0, all = 0;
        int cnt = 0, over = 0, overAll = 0;
        size_t logShown = 0;
        // per story state: CPU time (engine frame), GPU wait (glFinish after it), and the frame time
        // a pipelined app would see, max(CPU, GPU) - CPU and GPU overlap there
        struct StateStat {
            std::string name;
            int frames = 0, over = 0;
            double cpu = 0, gpu = 0, piped = 0, worst = 0;
            double render = 0;  // threaded: render thread replay
        };
        // threaded: per frame, written on the render thread (replay time, wait for the GPU fence)
        struct RenderStat {
            double start = 0, replay = 0, gpuWait = 0;
        };
        std::vector<RenderStat> rstat(static_cast<size_t>(std::max(0, frames)));
        std::vector<int> frameState(static_cast<size_t>(std::max(0, frames)), 0);
        std::deque<GLsync> fences;  // render thread only
        double lastEnd = 0;
        std::vector<StateStat> states(1);
        states[0].name = "(start)";
        // OYSTER_SKIP_TO=<state>: run the story without rendering until that state is entered
        if (const char* skipTo = std::getenv("OYSTER_SKIP_TO")) {
            engine.skipRender = true;
            double s0 = nowMs();
            int n = 0;
            bool reached = false;
            while (!reached && !engine.exitRequested && n < 200000) {
                engine.frame(0);
                ++n;
                const auto& log = engine.lua().log;
                for (; logShown < log.size(); ++logShown)
                    if (log[logShown].find(std::string("->[") + skipTo + "]") != std::string::npos) reached = true;
            }
            engine.skipRender = false;
            states.back().name = skipTo;
            std::printf("skipped %d frames to %s in %.0f ms\n", n, skipTo, nowMs() - s0);
        }
        for (int i = 0; i < frames && !engine.exitRequested; ++i) {
            size_t loads = rr.texLoads;
            double tex = rr.texReadMs + rr.texDecodeMs + rr.texUploadMs;
            double texR = rr.texReadMs, texU = rr.texUploadMs;
            gl::threaded::Stats tsBefore = gl::threaded::stats();
            double f0 = nowMs();
            RenderStat* rs = &rstat[static_cast<size_t>(i)];
            static const bool markFrames = std::getenv("OYSTER_DEBUG_GLSLOW") != nullptr;
            if (threaded)
                gl::threaded::enqueue([rs, i] {
                    rs->start = nowMs();
                    if (markFrames) std::fprintf(stderr, "render frame %d\n", i);
                });
            if (!engine.frame(0)) std::printf("frame %d: Lua error\n", i);
            double f1 = nowMs();
            gl::threaded::Stats tsAfter = gl::threaded::stats();
            double ms, cpuMs = f1 - f0, gpuWait;
            if (threaded) {
                // end of frame on the render thread: fence; keep at most two frames on the GPU
                gl::threaded::enqueue([rs, &fences] {
                    double t = nowMs();
                    rs->replay = t - rs->start;
                    fences.push_back(glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0));
                    while (fences.size() > 2) {
                        glClientWaitSync(fences.front(), GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull);
                        glDeleteSync(fences.front());
                        fences.pop_front();
                    }
                    rs->gpuWait = nowMs() - t;
                });
                gl::threaded::endFrame(1);
                double f2 = nowMs();
                ms = lastEnd > 0 ? f2 - lastEnd : f2 - f0;  // frame period
                lastEnd = f2;
                gpuWait = 0;
            } else {
                glFinish();
                ms = nowMs() - f0;
                gpuWait = ms - cpuMs;
            }
            frameState[static_cast<size_t>(i)] = static_cast<int>(states.size()) - 1;
            StateStat& st = states.back();
            ++st.frames;
            st.cpu += cpuMs;
            st.gpu += gpuWait;
            double piped = std::max(cpuMs, ms);  // GPU work >= the time we waited for it after submission
            piped = std::max(cpuMs, gpuWait + 0.0);
            st.piped += threaded ? ms : std::max(cpuMs, ms - cpuMs > 0 ? ms - cpuMs : 0.0);
            st.worst = std::max(st.worst, ms);
            if (threaded ? ms > budget * 1.02 : std::max(cpuMs, gpuWait) > budget) ++st.over;
            (void)piped;
            const auto& log = engine.lua().log;
            for (; logShown < log.size(); ++logShown) {
                const std::string& l = log[logShown];
                size_t arrow = l.find("]->[");
                if (arrow == std::string::npos) continue;
                std::printf("frame %d  %s\n", i, l.c_str() + 9);
                std::string dest = l.substr(arrow + 4, l.find(']', arrow + 4) - arrow - 4);
                if (dest.rfind("AudioTest", 0) == 0 || dest == "Preload") continue;
                states.push_back(StateStat());
                states.back().name = dest;
            }
            if (ms > 2 * budget)
                std::printf("slow frame %d: %.1f ms, engine thread %.1f ms (%zu texture loads %.1f ms: read %.1f, upload "
                            "%.1f; waited for the render thread %.1f ms in %llu sync calls)\n",
                            i, ms, cpuMs, rr.texLoads - loads, rr.texReadMs + rr.texDecodeMs + rr.texUploadMs - tex,
                            rr.texReadMs - texR, rr.texUploadMs - texU, tsAfter.waitMs - tsBefore.waitMs,
                            static_cast<unsigned long long>(tsAfter.syncCalls - tsBefore.syncCalls));
            sum += ms;
            all += ms;
            mx = std::max(mx, ms);
            if (ms > budget) { ++over; ++overAll; }
            if (++cnt == 360) {
                std::printf("frames %d-%d: avg %.2f ms max %.2f ms, %d over %.1f ms; textures read %.0f decode %.0f upload "
                            "%.0f ms (%zu loads), resident %.0f MB\n",
                            i - 359, i, sum / cnt, mx, over, budget, rr.texReadMs, rr.texDecodeMs, rr.texUploadMs, rr.texLoads,
                            static_cast<double>(rr.textureBytes()) / 1048576.0);
                sum = mx = 0;
                cnt = over = 0;
                rr.texReadMs = rr.texDecodeMs = rr.texUploadMs = 0;
                rr.texLoads = 0;
            }
        }
        if (threaded) {
            gl::threaded::finish();
            for (size_t f = 0; f < rstat.size(); ++f) {
                StateStat& st = states[static_cast<size_t>(frameState[f])];
                st.render += rstat[f].replay;
                st.gpu += rstat[f].gpuWait;
                if (rstat[f].replay + rstat[f].gpuWait > 2 * budget)
                    std::printf("render thread frame %zu: replay %.1f ms, GPU wait %.1f ms\n", f, rstat[f].replay,
                                rstat[f].gpuWait);
            }
            std::printf("\n%-34s %7s %8s %9s %8s %9s %9s %6s\n", "state", "frames", "main ms", "render ms", "GPU ms",
                        "frame ms", "max ms", "over");
            for (const StateStat& st : states) {
                if (!st.frames) continue;
                double n = st.frames;
                std::printf("%-34s %7d %8.2f %9.2f %8.2f %9.2f %9.1f %5.0f%%\n", st.name.c_str(), st.frames, st.cpu / n,
                            st.render / n, st.gpu / n, st.piped / n, st.worst, 100.0 * st.over / n);
            }
            gl::threaded::Stats ts = gl::threaded::stats();
            std::printf("(main = engine thread (story, animation, recording); render = GL replay on the render thread;\n"
                        " GPU = render thread waiting for the frame before last; frame = measured frame period;\n"
                        " over = frames longer than %.1f ms; %llu synchronous GL calls)\n",
                        budget, static_cast<unsigned long long>(ts.syncCalls));
        } else {
            std::printf("\n%-34s %7s %8s %8s %9s %9s %6s\n", "state", "frames", "CPU ms", "GPU ms", "frame ms", "max ms", "over");
            for (const StateStat& st : states) {
                if (!st.frames) continue;
                double n = st.frames;
                std::printf("%-34s %7d %8.2f %8.2f %9.2f %9.1f %5.0f%%\n", st.name.c_str(), st.frames, st.cpu / n, st.gpu / n,
                            st.piped / n, st.worst, 100.0 * st.over / n);
            }
            std::printf("(GPU ms = wait for the GPU after the CPU work; frame ms = max(CPU, GPU), the pipelined frame time;\n"
                        " over = frames whose CPU or GPU part exceeds %.1f ms)\n", budget);
        }
        std::printf("done: %llu frames, story time %.1f s, average %.2f ms per frame, %d frames over budget\n",
                    static_cast<unsigned long long>(engine.frameIndex), static_cast<double>(engine.time().elapsedUs) * 1e-6,
                    all / static_cast<double>(std::max<uint64_t>(1, engine.frameIndex)), overAll);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "self test failed: %s\n", e.what());
        gl::threaded::stop();
        return 1;
    }
    gl::threaded::stop();
    return 0;
}
