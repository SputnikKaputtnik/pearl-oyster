// oyster_player: runs the original story scripts of the user's Pearl installation on the
// Oyster engine layer (story/engine.h) - the film plays state by state from its own FSMs.
//
// Usage: oyster_player --root <install> [--package pearl_vrcam] [--size 1280x720] [--msaa 2]
//                      [--fixed 33.3333]  fixed time step in ms (default; 0 = real time)
//                      [--frames N]       stop after N frames (0 = until the story exits)
//                      [--out dir/%05d.tga --dump-from F --dump-every K]
//                      [--window] [--remaster] [--log] [--status K  (FSM status every K frames)]
#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fstream>
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

struct Args {
    std::string root, package = "pearl_vrcam", out, eval, watch, wav;
    int w = 1280, h = 720, msaa = 2;
    double fixedMs = 33.3333;
    long frames = 0, dumpFrom = 0, dumpEvery = 1, status = 0;
    bool window = false, remaster = false, log = false, mute = false;
    bool threaded = false;  // GL calls on a render thread (render/gl_thread.h)
    bool vr = false;  // desktop HMD emulation: stereo, head = mouse (docs/vr.md)
    int eyeW = 1024, eyeH = 1056;
};

bool parseArgs(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (k == "--root") a.root = next();
        else if (k == "--package") a.package = next();
        else if (k == "--size") { std::string s = next(); std::sscanf(s.c_str(), "%dx%d", &a.w, &a.h); }
        else if (k == "--msaa") a.msaa = std::stoi(next());
        else if (k == "--fixed") a.fixedMs = std::stod(next());
        else if (k == "--frames") a.frames = std::stol(next());
        else if (k == "--out") a.out = next();
        else if (k == "--dump-from") a.dumpFrom = std::stol(next());
        else if (k == "--dump-every") a.dumpEvery = std::stol(next());
        else if (k == "--window") a.window = true;
        else if (k == "--remaster") a.remaster = true;
        else if (k == "--log") a.log = true;
        else if (k == "--eval") a.eval = next();
        else if (k == "--watch") a.watch = next();
        else if (k == "--status") a.status = std::stol(next());
        else if (k == "--wav") a.wav = next();
        else if (k == "--mute") a.mute = true;
        else if (k == "--vr") a.vr = true;
        else if (k == "--threaded") a.threaded = true;
        else if (k == "--eye") { std::string s = next(); std::sscanf(s.c_str(), "%dx%d", &a.eyeW, &a.eyeH); }
        else {
            std::fprintf(stderr, "unknown argument %s\n", k.c_str());
            return false;
        }
    }
    return !a.root.empty();
}

void writeTGA(const std::string& path, int w, int h, const std::vector<uint8_t>& rgba) {
    std::ofstream f(path, std::ios::binary);
    uint8_t hdr[18] = {};
    hdr[2] = 2;
    hdr[12] = static_cast<uint8_t>(w & 255); hdr[13] = static_cast<uint8_t>(w >> 8);
    hdr[14] = static_cast<uint8_t>(h & 255); hdr[15] = static_cast<uint8_t>(h >> 8);
    hdr[16] = 24;
    f.write(reinterpret_cast<char*>(hdr), 18);
    std::vector<uint8_t> row(static_cast<size_t>(w) * 3);
    for (int y = 0; y < h; ++y) {  // GL rows are bottom-up = TGA default origin
        const uint8_t* src = &rgba[static_cast<size_t>(y) * w * 4];
        for (int x = 0; x < w; ++x) {
            row[3 * x] = src[4 * x + 2];
            row[3 * x + 1] = src[4 * x + 1];
            row[3 * x + 2] = src[4 * x];
        }
        f.write(reinterpret_cast<char*>(row.data()), static_cast<std::streamsize>(row.size()));
    }
}

// 16-bit stereo 48 kHz WAV (header patched on close)
struct WavWriter {
    std::ofstream f;
    uint32_t bytes = 0;
    explicit WavWriter(const std::string& path) : f(path, std::ios::binary) {
        char hdr[44] = {};
        f.write(hdr, 44);
    }
    void write(const int16_t* d, uint32_t frames) {
        f.write(reinterpret_cast<const char*>(d), static_cast<std::streamsize>(frames) * 4);
        bytes += frames * 4;
    }
    ~WavWriter() {
        auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
        auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
        f.seekp(0);
        f.write("RIFF", 4); u32(36 + bytes); f.write("WAVEfmt ", 8); u32(16); u16(1); u16(2); u32(48000);
        u32(48000 * 4); u16(4); u16(16); f.write("data", 4); u32(bytes);
    }
};

void audioCallback(void* user, Uint8* stream, int len) {
    static_cast<audio::Engine*>(user)->render(reinterpret_cast<int16_t*>(stream), static_cast<uint32_t>(len / 4));
}

}  // namespace

int main(int argc, char** argv) {
    Args args;
    if (!parseArgs(argc, argv, args)) {
        std::fprintf(stderr, "usage: oyster_player --root <install> [--frames N] [--out dir/%%05d.tga] [--window]\n");
        return 2;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) { std::fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    Uint32 flags = SDL_WINDOW_OPENGL | (args.window ? SDL_WINDOW_SHOWN : SDL_WINDOW_HIDDEN);
    SDL_Window* win = SDL_CreateWindow("Oyster - Pearl", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       args.window ? args.w : 64, args.window ? args.h : 64, flags);
    if (!win) { std::fprintf(stderr, "window: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) { std::fprintf(stderr, "GLES 3.0 context: %s\n", SDL_GetError()); return 1; }
    if (const char* missing = gl::load(SDL_GL_GetProcAddress)) { std::fprintf(stderr, "missing GL function %s\n", missing); return 1; }
    if (args.window) SDL_GL_SetSwapInterval(1);
    if (args.threaded) {
        SDL_GL_MakeCurrent(win, nullptr);
        gl::threaded::start([win, ctx] { SDL_GL_MakeCurrent(win, ctx); }, [win] { SDL_GL_MakeCurrent(win, nullptr); });
    }

    int rc = 0;
    try {
        PackageFS fs(args.root);
        story::EngineOptions opt;
        opt.package = args.package;
        opt.width = args.w;
        opt.height = args.h;
        opt.msaa = args.msaa;
        opt.prefetchHints = "prefetch_hints.txt";  // working directory (never the installation)
        opt.policy = args.remaster ? SamplingPolicy::remaster() : SamplingPolicy::original();
        story::Engine engine(fs, opt);
        // Emulated HMD (set before boot: the camera rig asks DisplayDevice.getType on creation):
        // seated (head 1.2 m above the tracking-space floor - Pearl waits for a seated viewer), IPD 63 mm, a Quest-3-like asymmetric
        // per-eye field of view. The head turns with the mouse (left button), see below.
        float headYaw = 0, headPitch = 0, headHeight = 1.2f;
        if (args.vr) {
            story::HmdState& h = engine.hmd();
            h.active = true;

            h.ipd = 0.063f;
            h.width = args.eyeW;
            h.height = args.eyeH;
            h.eye[0] = {-1.13f, 0.84f, 0.90f, -1.13f};
            h.eye[1] = {-0.84f, 1.13f, 0.90f, -1.13f};
            if (const char* hd = std::getenv("OYSTER_DEBUG_HEAD")) std::sscanf(hd, "%f,%f,%f", &headYaw, &headPitch, &headHeight);
            h.position = Vec3(0, headHeight, 0);
        }
        engine.lua().echoLog = args.log;
        engine.time().fixedStepMs = args.fixedMs;
        if (!engine.boot()) throw std::runtime_error("story boot failed: " + engine.lua().lastError());
        std::printf("story booted\n");
        if (!args.eval.empty()) engine.lua().doString(args.eval, "=eval");

        // Audio: real time through SDL (48 kHz, s16 stereo, 512-frame blocks like the original),
        // or rendered in lockstep with story time into a WAV file (--wav)
        SDL_AudioDeviceID dev = 0;
        std::unique_ptr<WavWriter> wav;
        uint64_t wavFrames = 0;
        if (!args.wav.empty()) {
            wav = std::make_unique<WavWriter>(args.wav);
        } else if (!args.mute) {
            SDL_AudioSpec want{}, have{};
            want.freq = audio::Engine::kRate;
            want.format = AUDIO_S16SYS;
            want.channels = 2;
            want.samples = audio::Engine::kBlock;
            want.callback = audioCallback;
            want.userdata = &engine.audio();
            dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
            if (!dev) std::fprintf(stderr, "audio device: %s\n", SDL_GetError());
            else SDL_PauseAudioDevice(dev, 0);
        }
        std::vector<int16_t> wavBuf;

        std::vector<uint8_t> px;
        auto last = std::chrono::steady_clock::now();
        size_t logShown = 0;
        bool quit = false;
        for (long f = 0; !quit && (args.frames == 0 || f < args.frames); ++f) {
            SDL_Event ev;
            float wheel = 0;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) quit = true;
                else if (ev.type == SDL_MOUSEWHEEL) wheel += static_cast<float>(ev.wheel.y);
            }
            if (args.vr) {
                // head orientation: yaw about +Y, then pitch about the head's X axis
                if (args.window) {
                    int dx = 0, dy = 0;
                    Uint32 mb = SDL_GetRelativeMouseState(&dx, &dy);
                    if (mb & SDL_BUTTON_LMASK) {
                        headYaw -= static_cast<float>(dx) * 0.004f;
                        headPitch = std::max(-1.5f, std::min(1.5f, headPitch - static_cast<float>(dy) * 0.004f));
                    }
                }
                Quat qy(0, std::sin(headYaw * 0.5f), 0, std::cos(headYaw * 0.5f));
                Quat qp(std::sin(headPitch * 0.5f), 0, 0, std::cos(headPitch * 0.5f));
                engine.hmd().orientation = qy * qp;
            } else if (args.window) {
                int mx = 0, my = 0;
                Uint32 mb = SDL_GetMouseState(&mx, &my);
                const bool b[3] = {(mb & SDL_BUTTON_LMASK) != 0, (mb & SDL_BUTTON_MMASK) != 0,
                                   (mb & SDL_BUTTON_RMASK) != 0};
                engine.input().update(static_cast<float>(mx), static_cast<float>(my), b, wheel);
            } else if (const char* drag = std::getenv("OYSTER_DEBUG_DRAG")) {
                // "first,dx,dy": headless left-button drag from frame `first` on, dx/dy pixels per frame
                long first = 0;
                float dx = 0, dy = 0;
                std::sscanf(drag, "%ld,%f,%f", &first, &dx, &dy);
                const bool b[3] = {f >= first, false, false};
                const story::InputState& in = engine.input();
                float x = in.mouseX + (f > first ? dx : 0), y = in.mouseY + (f > first ? dy : 0);
                engine.input().update(x, y, b, 0);
            }
            auto now = std::chrono::steady_clock::now();
            double dt = std::chrono::duration<double>(now - last).count();
            last = now;
            if (!engine.frame(dt)) {
                std::fprintf(stderr, "frame %ld: lua error\n", f);
                rc = 1;
                break;
            }
            if (wav) {
                uint64_t target = static_cast<uint64_t>(engine.time().elapsedUs) * 48000 / 1000000;
                if (target > wavFrames) {
                    uint32_t n = static_cast<uint32_t>(target - wavFrames);
                    wavBuf.resize(static_cast<size_t>(n) * 2);
                    engine.audio().render(wavBuf.data(), n);
                    wav->write(wavBuf.data(), n);
                    wavFrames = target;
                }
            }
            // state transitions as logged by FSM:onTransition ("[old]->[new]")
            const auto& log = engine.lua().log;
            for (; logShown < log.size(); ++logShown)
                if (log[logShown].find("]->[") != std::string::npos)
                    std::printf("frame %ld  t=%.3f  %s\n", f, static_cast<double>(engine.time().elapsedUs) * 1e-6,
                                log[logShown].c_str() + 9);
            if (args.status > 0 && f % args.status == 0) {
                char code[160];
                std::snprintf(code, sizeof(code), "local f = %ld\n", f);
                engine.lua().doString(std::string(code) + R"lua(
local s = "frame " .. f .. string.format(" t=%.3f", Time.seconds())
if Global.story:isBuffering() then s = s .. " BUFFERING" end
for _, fsm in ipairs(Global.story.statemachines) do
  local st = fsm.currstate
  if st then
    s = s .. string.format("  %s:%s %.3f/%s loop %d", fsm.name, st.name, st:localTime(), tostring(st.def.duration), st.loopcounter)
  end
end
print(s))lua", "=status");
                if (!args.watch.empty()) engine.lua().doString(std::string(code) + args.watch, "=watch");
            }
            if (args.vr) {
                const RenderTarget* e0 = engine.eyeOutput(0);
                const RenderTarget* e1 = engine.eyeOutput(1);
                if (e0 && e1 && !args.out.empty() && f >= args.dumpFrom && (f - args.dumpFrom) % args.dumpEvery == 0) {
                    // both eyes side by side (left | right)
                    int w = e0->width, h = e0->height;
                    std::vector<uint8_t> eyePx(static_cast<size_t>(w) * h * 4);
                    px.assign(static_cast<size_t>(w) * 2 * h * 4, 0);
                    for (int e = 0; e < 2; ++e) {
                        glBindFramebuffer(GL_FRAMEBUFFER, (e ? e1 : e0)->fbo);
                        glPixelStorei(GL_PACK_ALIGNMENT, 1);
                        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, eyePx.data());
                        for (int y = 0; y < h; ++y)
                            std::memcpy(&px[(static_cast<size_t>(y) * w * 2 + static_cast<size_t>(e) * w) * 4],
                                        &eyePx[static_cast<size_t>(y) * w * 4], static_cast<size_t>(w) * 4);
                    }
                    char name[1024];
                    std::snprintf(name, sizeof(name), args.out.c_str(), static_cast<int>(f));
                    writeTGA(name, w * 2, h, px);
                }
                if (args.window && e0 && e1) {
                    glDisable(GL_SCISSOR_TEST);
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, e0->fbo);
                    glBlitFramebuffer(0, 0, e0->width, e0->height, 0, 0, args.w / 2, args.h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, e1->fbo);
                    glBlitFramebuffer(0, 0, e1->width, e1->height, args.w / 2, 0, args.w, args.h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
                    gl::threaded::enqueue([win] { SDL_GL_SwapWindow(win); });
                }
                gl::threaded::endFrame(1);
                if (engine.exitRequested) break;
                continue;
            }
            const RenderTarget* out = engine.output();
            if (out && !args.out.empty() && f >= args.dumpFrom && (f - args.dumpFrom) % args.dumpEvery == 0) {
                px.resize(static_cast<size_t>(out->width) * out->height * 4);
                glBindFramebuffer(GL_FRAMEBUFFER, out->fbo);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(0, 0, out->width, out->height, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                char name[1024];
                std::snprintf(name, sizeof(name), args.out.c_str(), static_cast<int>(f));
                writeTGA(name, out->width, out->height, px);
            }
            if (args.window && out) {
                glDisable(GL_SCISSOR_TEST);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, out->fbo);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                glBlitFramebuffer(0, 0, out->width, out->height, 0, 0, args.w, args.h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
                gl::threaded::enqueue([win] { SDL_GL_SwapWindow(win); });
            }
            gl::threaded::endFrame(1);
            if (engine.exitRequested) break;
        }
        if (dev) SDL_CloseAudioDevice(dev);
        if (args.threaded) {
            gl::threaded::Stats ts = gl::threaded::stats();
            std::printf("render thread: %llu frames, replay %.2f ms/frame, engine thread waited %.2f ms/frame, %llu sync "
                        "calls, %.2f MB/frame recorded\n",
                        static_cast<unsigned long long>(ts.frames), ts.replayMs / std::max<double>(1, ts.frames),
                        ts.waitMs / std::max<double>(1, ts.frames), static_cast<unsigned long long>(ts.syncCalls),
                        static_cast<double>(ts.bytes) / 1048576.0 / std::max<double>(1, ts.frames));
        }
        std::printf("frames: %llu, story time %.3f s\n", static_cast<unsigned long long>(engine.frameIndex),
                    static_cast<double>(engine.time().elapsedUs) * 1e-6);
        std::printf("unimplemented original natives called:\n");
        for (const auto& m : engine.lua().missing) std::printf("  %-48s %d\n", m.first.c_str(), m.second);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        rc = 1;
    }
    if (args.threaded) {
        gl::threaded::stop();
        SDL_GL_MakeCurrent(win, ctx);
    }
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return rc;
}
