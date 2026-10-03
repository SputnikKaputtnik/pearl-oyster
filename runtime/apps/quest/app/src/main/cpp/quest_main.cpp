// Oyster player for Meta Quest: OpenXR (GLES 3) platform layer around the shared story engine.
//
// The original Lua story runs exactly as on the desktop (Platform = Windows candidate build);
// the display device is an HMD (story::HmdState, docs/vr.md): head pose and per-eye field of
// view come from xrLocateViews for the predicted display time, the engine renders the active
// render graph once per eye, and the eye images are copied into the OpenXR swapchains.
// Pearl's data is the user's own installation in <external files dir>/pearl (README.md).
#include <EGL/egl.h>
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <dlfcn.h>
#include <jni.h>
#include <pthread.h>
#include <unistd.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/pkgfs.h"
#include "render/gl.h"
#include "render/gl_thread.h"
#include "render/renderer.h"
#include "story/engine.h"

using namespace oyster;
using namespace oyster::gl;

#ifndef GL_FRAMEBUFFER_SRGB_EXT
#define GL_FRAMEBUFFER_SRGB_EXT 0x8DB9
#endif
#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif

namespace {

constexpr char kTag[] = "OysterPearl";
constexpr XrViewConfigurationType kViewConfig = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
constexpr size_t kTextureBudget = size_t(1024) << 20;  // decoded textures kept resident
// Eye image size = the runtime's recommended size (1680x1760 on Quest 3) times this; the
// original's SteamVR eye targets were 2612x2852. Measured with the self test (README.md).
constexpr float kDefaultResolutionScale = 1.3f;  // 2184x2288: heaviest shot 9.2 ms (self test)

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, kTag, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, kTag, __VA_ARGS__)

// stdout/stderr of the engine (Lua log, warnings) -> logcat
void redirectStdio() {
    static int fds[2];
    if (pipe(fds) != 0) return;
    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    dup2(fds[1], 1);
    dup2(fds[1], 2);
    std::thread([] {
        std::string line;
        char buf[512];
        ssize_t n;
        while ((n = read(fds[0], buf, sizeof(buf))) > 0)
            for (ssize_t i = 0; i < n; ++i) {
                if (buf[i] == '\n') {
                    __android_log_write(ANDROID_LOG_INFO, "OysterEngine", line.c_str());
                    line.clear();
                } else {
                    line += buf[i];
                }
            }
    }).detach();
}

void* glProc(const char* name) {
    static void* lib = dlopen("libGLESv3.so", RTLD_NOW | RTLD_LOCAL);
    void* p = lib ? dlsym(lib, name) : nullptr;
    if (!p) p = reinterpret_cast<void*>(eglGetProcAddress(name));
    return p;
}

bool xrOk(const char* what, XrResult r) {
    if (XR_SUCCEEDED(r)) return true;
    LOGE("%s failed: %d", what, static_cast<int>(r));
    return false;
}

struct Swapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    int width = 0, height = 0;
    std::vector<XrSwapchainImageOpenGLESKHR> images;
    std::vector<GLuint> fbos;
};

struct App {
    android_app* android = nullptr;
    bool resumed = false;
    // EGL
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLConfig config = nullptr;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    // OpenXR
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    XrSession session = XR_NULL_HANDLE;
    XrSpace space = XR_NULL_HANDLE;
    XrSpace viewSpace = XR_NULL_HANDLE;  // head-locked overlay (shader warm-up message)
    Swapchain overlay;
    XrSessionState state = XR_SESSION_STATE_UNKNOWN;
    bool running = false;
    std::vector<XrViewConfigurationView> configViews;
    Swapchain swapchains[2];
    bool srgbWriteControl = false;
    // engine
    std::unique_ptr<PackageFS> fs;
    std::unique_ptr<story::Engine> engine;
    bool warmup = false;  // shaders are being prepared before the story boots
    bool engineFailed = false;
    XrTime lastDisplayTime = 0;
    AAudioStream* audio = nullptr;
    bool audioPlaying = false;
    uint64_t frames = 0;
    double statMs = 0, statMax = 0;
    int statFrames = 0;
    gl::threaded::Stats statThread;  // render thread totals at the last perf line
    // settings (/sdcard/Oyster/oyster.cfg)
    float resolutionScale = kDefaultResolutionScale;
    bool interpolate = false;  // remaster: interpolate the original's stepped animation keys
    int msaa = 2;
    // metres, display only (HmdState::eyeRaise); thumbstick up/down. Default 0.30: chosen by the
    // project owner in the headset (eye level with Dad in the car); 0 = the original framing.
    float eyeRaise = 0.30f;
    // controller: A (right) / X (left) toggles the animation interpolation, the thumbsticks
    // (up/down) raise or lower the eye height in 5 cm steps
    XrActionSet actionSet = XR_NULL_HANDLE;
    XrAction toggleAction = XR_NULL_HANDLE;
    XrAction heightAction = XR_NULL_HANDLE;
    int heightStick = 0;  // -1 / 0 / +1: last stick zone (one step per push)
    std::string notice;       // shown on a head-locked panel for a moment
    XrTime noticeUntil = 0;
};

// Pearl data: shared storage /sdcard/Oyster/pearl (adb-pushed, needs "all files access"), else
// the app's own external files directory
constexpr char kSharedRoot[] = "/sdcard/Oyster";
std::string dataRoot(const std::string& files) {
    std::string shared = std::string(kSharedRoot) + "/pearl";
    std::error_code ec;
    if (std::filesystem::is_directory(shared, ec)) return shared;
    return files + "/pearl";
}

aaudio_data_callback_result_t audioCallback(AAudioStream*, void* user, void* data, int32_t frames) {
    static_cast<audio::Engine*>(user)->render(static_cast<int16_t*>(data), static_cast<uint32_t>(frames));
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void setAudio(App& a, bool play) {
    if (!a.audio || play == a.audioPlaying) return;
    if (play) AAudioStream_requestStart(a.audio);
    else AAudioStream_requestPause(a.audio);
    a.audioPlaying = play;
}

bool initLoader(App& a) {
    PFN_xrInitializeLoaderKHR init = nullptr;
    if (!xrOk("xrGetInstanceProcAddr", xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
                                                             reinterpret_cast<PFN_xrVoidFunction*>(&init))) || !init)
        return false;
    XrLoaderInitInfoAndroidKHR li{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    li.applicationVM = a.android->activity->vm;
    li.applicationContext = a.android->activity->clazz;
    return xrOk("xrInitializeLoaderKHR", init(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&li)));
}

bool initEgl(App& a) {
    a.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (a.display == EGL_NO_DISPLAY || !eglInitialize(a.display, nullptr, nullptr) || !eglBindAPI(EGL_OPENGL_ES_API))
        return false;
    const EGLint cfg[] = {EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 0,
                          EGL_RENDERABLE_TYPE, 0x40 /* EGL_OPENGL_ES3_BIT */, EGL_NONE};
    EGLint n = 0;
    if (!eglChooseConfig(a.display, cfg, &a.config, 1, &n) || n != 1) return false;
    const EGLint ctx[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    a.context = eglCreateContext(a.display, a.config, EGL_NO_CONTEXT, ctx);
    const EGLint pb[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    a.surface = eglCreatePbufferSurface(a.display, a.config, pb);
    if (a.context == EGL_NO_CONTEXT || a.surface == EGL_NO_SURFACE) return false;
    if (!eglMakeCurrent(a.display, a.surface, a.surface, a.context)) return false;
    if (const char* missing = gl::load(glProc)) {
        LOGE("missing GL function %s", missing);
        return false;
    }
    const char* ext = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    a.srgbWriteControl = ext && std::strstr(ext, "GL_EXT_sRGB_write_control");
    LOGI("GL %s / %s, sRGB write control %d", glGetString(GL_RENDERER), glGetString(GL_VERSION), a.srgbWriteControl);
    return true;
}

bool createSwapchains(App& a) {
    uint32_t n = 0;
    xrEnumerateViewConfigurationViews(a.instance, a.system, kViewConfig, 0, &n, nullptr);
    if (n != 2) return false;
    a.configViews.assign(2, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    if (!xrOk("xrEnumerateViewConfigurationViews",
              xrEnumerateViewConfigurationViews(a.instance, a.system, kViewConfig, 2, &n, a.configViews.data())))
        return false;
    uint32_t fc = 0;
    xrEnumerateSwapchainFormats(a.session, 0, &fc, nullptr);
    std::vector<int64_t> formats(fc);
    xrEnumerateSwapchainFormats(a.session, fc, &fc, formats.data());
    // The engine's output is display-referred (the shaders apply their own gamma), exactly what
    // the original handed to the compositor. An sRGB swapchain receives these bytes unchanged
    // (sRGB encoding on write switched off), so the compositor shows them as the original did.
    int64_t format = GL_RGBA8;
    if (a.srgbWriteControl && std::find(formats.begin(), formats.end(), GL_SRGB8_ALPHA8) != formats.end())
        format = GL_SRGB8_ALPHA8;
    for (int e = 0; e < 2; ++e) {
        Swapchain& s = a.swapchains[e];
        XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        ci.format = format;
        ci.sampleCount = 1;
        const XrViewConfigurationView& cv = a.configViews[e];
        ci.width = std::min(cv.maxImageRectWidth, static_cast<uint32_t>(std::lround(cv.recommendedImageRectWidth * a.resolutionScale)));
        ci.height = std::min(cv.maxImageRectHeight, static_cast<uint32_t>(std::lround(cv.recommendedImageRectHeight * a.resolutionScale)));
        ci.faceCount = 1;
        ci.arraySize = 1;
        ci.mipCount = 1;
        if (!xrOk("xrCreateSwapchain", xrCreateSwapchain(a.session, &ci, &s.handle))) return false;
        s.width = static_cast<int>(ci.width);
        s.height = static_cast<int>(ci.height);
        uint32_t ic = 0;
        xrEnumerateSwapchainImages(s.handle, 0, &ic, nullptr);
        s.images.assign(ic, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
        xrEnumerateSwapchainImages(s.handle, ic, &ic, reinterpret_cast<XrSwapchainImageBaseHeader*>(s.images.data()));
        s.fbos.resize(ic);
        glGenFramebuffers(static_cast<GLsizei>(ic), s.fbos.data());
        for (uint32_t i = 0; i < ic; ++i) {
            glBindFramebuffer(GL_FRAMEBUFFER, s.fbos[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s.images[i].image, 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    LOGI("swapchains %dx%d per eye, format 0x%llx", a.swapchains[0].width, a.swapchains[0].height,
         static_cast<unsigned long long>(format));
    return true;
}

// /sdcard/Oyster/oyster.cfg: "key = value" lines, # comments; written with the defaults when missing.
void loadConfig(App& a) {
    std::string path = std::string(kSharedRoot) + "/oyster.cfg";
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (std::FILE* w = std::fopen(path.c_str(), "wb")) {
            std::fprintf(w,
                         "# Oyster - Pearl settings (read at start)\n"
                         "# eye image size = the headset's recommended size x resolution_scale (0.5 .. 2.0)\n"
                         "resolution_scale = %.2f\n"
                         "# animation: original (stepped keys as authored) or interpolated (remaster);\n"
                         "# the A / X controller button switches it while the story plays\n"
                         "animation = original\n"
                         "# multisampling of the eye images: 1, 2 (the original's -msaa 2) or 4\n"
                         "msaa = 2\n"
                         "# not in the original: eye cameras raised by this many metres (display only);\n"
                         "# the thumbsticks (up/down) change it in 5 cm steps and save it here\n"
                         "eye_height_offset = 0.30\n",
                         kDefaultResolutionScale);
            std::fclose(w);
        }
    } else {
        char line[256];
        while (std::fgets(line, sizeof(line), f)) {
            std::string l(line);
            size_t hash = l.find('#');
            if (hash != std::string::npos) l.erase(hash);
            size_t eq = l.find('=');
            if (eq == std::string::npos) continue;
            auto trim = [](std::string s) {
                while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
                while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(0, 1);
                return s;
            };
            std::string k = trim(l.substr(0, eq)), v = trim(l.substr(eq + 1));
            if (k == "resolution_scale") a.resolutionScale = std::max(0.5f, std::min(2.0f, std::strtof(v.c_str(), nullptr)));
            else if (k == "animation") a.interpolate = v == "interpolated";
            else if (k == "msaa") a.msaa = std::max(1, std::min(4, std::atoi(v.c_str())));
            else if (k == "eye_height_offset") a.eyeRaise = std::max(-1.0f, std::min(1.0f, std::strtof(v.c_str(), nullptr)));
        }
        std::fclose(f);
    }
    LOGI("settings: resolution_scale %.2f, animation %s, msaa %d, eye_height_offset %.2f", a.resolutionScale,
         a.interpolate ? "interpolated" : "original", a.msaa, a.eyeRaise);
}

// Writes one "key = value" line of oyster.cfg (replaced in place, else appended).
void saveSetting(const std::string& key, const std::string& value) {
    std::string path = std::string(kSharedRoot) + "/oyster.cfg";
    std::vector<std::string> lines;
    if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
        char line[256];
        while (std::fgets(line, sizeof(line), f)) lines.emplace_back(line);
        std::fclose(f);
    }
    bool found = false;
    for (std::string& l : lines) {
        size_t p = l.find_first_not_of(" \t");  // "<key> = ..." (not a comment, not a longer key)
        size_t q = p == std::string::npos ? p : l.find_first_not_of(" \t", p + key.size());
        if (p != std::string::npos && l.compare(p, key.size(), key) == 0 && q != std::string::npos && l[q] == '=') {
            l = key + " = " + value + "\n";
            found = true;
        }
    }
    if (!found) lines.push_back(key + " = " + value + "\n");
    if (std::FILE* f = std::fopen(path.c_str(), "wb")) {
        for (const std::string& l : lines) std::fputs(l.c_str(), f);
        std::fclose(f);
    }
}

// Controller button A (right) / X (left): toggles the animation interpolation.
void createActions(App& a) {
    XrActionSetCreateInfo si{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::snprintf(si.actionSetName, XR_MAX_ACTION_SET_NAME_SIZE, "oyster");
    std::snprintf(si.localizedActionSetName, XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE, "Oyster");
    if (!xrOk("xrCreateActionSet", xrCreateActionSet(a.instance, &si, &a.actionSet))) return;
    XrActionCreateInfo ac{XR_TYPE_ACTION_CREATE_INFO};
    ac.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    std::snprintf(ac.actionName, XR_MAX_ACTION_NAME_SIZE, "toggle_interpolation");
    std::snprintf(ac.localizedActionName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE, "Toggle animation interpolation");
    if (!xrOk("xrCreateAction", xrCreateAction(a.actionSet, &ac, &a.toggleAction))) return;
    ac.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
    std::snprintf(ac.actionName, XR_MAX_ACTION_NAME_SIZE, "eye_height");
    std::snprintf(ac.localizedActionName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE, "Eye height");
    if (!xrOk("xrCreateAction", xrCreateAction(a.actionSet, &ac, &a.heightAction))) return;
    XrPath profile = XR_NULL_PATH, aButton = XR_NULL_PATH, xButton = XR_NULL_PATH, rStick = XR_NULL_PATH,
           lStick = XR_NULL_PATH;
    xrStringToPath(a.instance, "/interaction_profiles/oculus/touch_controller", &profile);
    xrStringToPath(a.instance, "/user/hand/right/input/a/click", &aButton);
    xrStringToPath(a.instance, "/user/hand/left/input/x/click", &xButton);
    xrStringToPath(a.instance, "/user/hand/right/input/thumbstick/y", &rStick);
    xrStringToPath(a.instance, "/user/hand/left/input/thumbstick/y", &lStick);
    XrActionSuggestedBinding b[] = {{a.toggleAction, aButton},
                                    {a.toggleAction, xButton},
                                    {a.heightAction, rStick},
                                    {a.heightAction, lStick}};
    XrInteractionProfileSuggestedBinding sb{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    sb.interactionProfile = profile;
    sb.suggestedBindings = b;
    sb.countSuggestedBindings = 4;
    xrOk("xrSuggestInteractionProfileBindings", xrSuggestInteractionProfileBindings(a.instance, &sb));
    XrSessionActionSetsAttachInfo at{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    at.countActionSets = 1;
    at.actionSets = &a.actionSet;
    xrOk("xrAttachSessionActionSets", xrAttachSessionActionSets(a.session, &at));
}

// Engine thread, once per frame while focused.
void pollActions(App& a, XrTime now) {
    if (!a.toggleAction || !a.engine) return;
    XrActiveActionSet active{a.actionSet, XR_NULL_PATH};
    XrActionsSyncInfo sy{XR_TYPE_ACTIONS_SYNC_INFO};
    sy.countActiveActionSets = 1;
    sy.activeActionSets = &active;
    if (XR_FAILED(xrSyncActions(a.session, &sy))) return;
    // eye height: one 5 cm step per push of a thumbstick up or down
    if (a.heightAction) {
        XrActionStateGetInfo hi{XR_TYPE_ACTION_STATE_GET_INFO};
        hi.action = a.heightAction;
        XrActionStateFloat hs{XR_TYPE_ACTION_STATE_FLOAT};
        if (XR_SUCCEEDED(xrGetActionStateFloat(a.session, &hi, &hs)) && hs.isActive) {
            int zone = hs.currentState > 0.7f ? 1 : hs.currentState < -0.7f ? -1 : 0;
            if (zone != 0 && zone != a.heightStick) {
                a.eyeRaise = std::max(-0.5f, std::min(0.8f, std::round((a.eyeRaise + 0.05f * static_cast<float>(zone)) * 20.0f) / 20.0f));
                a.engine->hmd().eyeRaise = a.eyeRaise;
                char text[48];
                std::snprintf(text, sizeof(text), "EYE HEIGHT %+d CM", static_cast<int>(std::lround(a.eyeRaise * 100.0f)));
                a.notice = text;
                a.noticeUntil = now + 2000000000;
                char v[16];
                std::snprintf(v, sizeof(v), "%.2f", a.eyeRaise);
                saveSetting("eye_height_offset", v);
                LOGI("eye height offset %.2f m", a.eyeRaise);
            }
            if (std::fabs(hs.currentState) < 0.3f) a.heightStick = 0;
            else if (zone != 0) a.heightStick = zone;
        }
    }
    XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
    gi.action = a.toggleAction;
    XrActionStateBoolean st{XR_TYPE_ACTION_STATE_BOOLEAN};
    if (XR_FAILED(xrGetActionStateBoolean(a.session, &gi, &st))) return;
    if (!(st.isActive && st.changedSinceLastSync && st.currentState)) return;
    a.interpolate = !a.interpolate;
    SamplingPolicy& p = a.engine->policy();
    p = a.interpolate ? SamplingPolicy::remaster() : SamplingPolicy::original();
    a.notice = a.interpolate ? "ANIMATION INTERPOLATED" : "ANIMATION ORIGINAL";
    a.noticeUntil = now + 2000000000;  // 2 s
    LOGI("animation %s", a.interpolate ? "interpolated" : "original");
}

bool initXr(App& a) {
    if (a.instance != XR_NULL_HANDLE) return true;
    if (!initLoader(a)) return false;
    XrInstanceCreateInfoAndroidKHR ai{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    ai.applicationVM = a.android->activity->vm;
    ai.applicationActivity = a.android->activity->clazz;
    uint32_t extCount = 0;
    xrEnumerateInstanceExtensionProperties(nullptr, 0, &extCount, nullptr);
    std::vector<XrExtensionProperties> exts(extCount, {XR_TYPE_EXTENSION_PROPERTIES});
    xrEnumerateInstanceExtensionProperties(nullptr, extCount, &extCount, exts.data());
    auto has = [&](const char* n) {
        return std::any_of(exts.begin(), exts.end(), [&](const XrExtensionProperties& e) { return !std::strcmp(e.extensionName, n); });
    };
    std::vector<const char*> enable = {XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME, XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME};
    bool localFloor = has("XR_EXT_local_floor");
    if (localFloor) enable.push_back("XR_EXT_local_floor");
    bool perfSettings = has(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME);
    if (perfSettings) enable.push_back(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME);
    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    ci.next = &ai;
    ci.enabledExtensionCount = static_cast<uint32_t>(enable.size());
    ci.enabledExtensionNames = enable.data();
    std::snprintf(ci.applicationInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE, "Oyster - Pearl");
    std::snprintf(ci.applicationInfo.engineName, XR_MAX_ENGINE_NAME_SIZE, "Oyster");
    ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    if (!xrOk("xrCreateInstance", xrCreateInstance(&ci, &a.instance))) return false;
    XrSystemGetInfo si{XR_TYPE_SYSTEM_GET_INFO};
    si.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!xrOk("xrGetSystem", xrGetSystem(a.instance, &si, &a.system))) return false;
    loadConfig(a);
    PFN_xrGetOpenGLESGraphicsRequirementsKHR req = nullptr;
    xrGetInstanceProcAddr(a.instance, "xrGetOpenGLESGraphicsRequirementsKHR", reinterpret_cast<PFN_xrVoidFunction*>(&req));
    XrGraphicsRequirementsOpenGLESKHR gr{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
    if (!req || !xrOk("xrGetOpenGLESGraphicsRequirementsKHR", req(a.instance, a.system, &gr))) return false;
    if (!initEgl(a)) {
        LOGE("EGL / GLES 3 setup failed");
        return false;
    }
    XrGraphicsBindingOpenGLESAndroidKHR gb{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    gb.display = a.display;
    gb.config = a.config;
    gb.context = a.context;
    XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
    sci.next = &gb;
    sci.systemId = a.system;
    if (!xrOk("xrCreateSession", xrCreateSession(a.instance, &sci, &a.session))) return false;
    // The original ran in OpenVR's standing universe (floor origin, isRoomBasedVR): LOCAL_FLOOR
    XrReferenceSpaceCreateInfo rs{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rs.referenceSpaceType = localFloor ? static_cast<XrReferenceSpaceType>(1000426000) /* LOCAL_FLOOR_EXT */
                                       : XR_REFERENCE_SPACE_TYPE_STAGE;
    rs.poseInReferenceSpace.orientation.w = 1;
    if (!xrOk("xrCreateReferenceSpace", xrCreateReferenceSpace(a.session, &rs, &a.space))) return false;
    LOGI("reference space %s", localFloor ? "LOCAL_FLOOR" : "STAGE");
    // The story renders the whole render graph twice per frame: ask for sustained high CPU and GPU
    // clocks (the default level is chosen for light apps)
    PFN_xrPerfSettingsSetPerformanceLevelEXT setLevel = nullptr;
    if (perfSettings &&
        XR_SUCCEEDED(xrGetInstanceProcAddr(a.instance, "xrPerfSettingsSetPerformanceLevelEXT",
                                           reinterpret_cast<PFN_xrVoidFunction*>(&setLevel))) && setLevel) {
        bool cpu = xrOk("perf level CPU", setLevel(a.session, XR_PERF_SETTINGS_DOMAIN_CPU_EXT, XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT));
        bool gpu = xrOk("perf level GPU", setLevel(a.session, XR_PERF_SETTINGS_DOMAIN_GPU_EXT, XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT));
        LOGI("performance level sustained high: CPU %d GPU %d", cpu, gpu);
    }
    XrReferenceSpaceCreateInfo vs{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    vs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    vs.poseInReferenceSpace.orientation.w = 1;
    xrOk("xrCreateReferenceSpace(VIEW)", xrCreateReferenceSpace(a.session, &vs, &a.viewSpace));
    createActions(a);
    return createSwapchains(a);
}

// ---- warm-up overlay: "COMPILING SHADERS nn%" and a progress bar, drawn with scissored clears
// (no shaders needed while the shaders are being built) into a head-locked quad layer.
constexpr int kOverlayW = 1024, kOverlayH = 256;

const uint8_t* glyph(char c) {
    static const struct { char c; uint8_t rows[7]; } font[] = {
        {'C', {14, 17, 16, 16, 16, 17, 14}}, {'O', {14, 17, 17, 17, 17, 17, 14}}, {'M', {17, 27, 21, 21, 17, 17, 17}},
        {'P', {30, 17, 17, 30, 16, 16, 16}}, {'I', {14, 4, 4, 4, 4, 4, 14}},      {'L', {16, 16, 16, 16, 16, 16, 31}},
        {'N', {17, 25, 21, 19, 17, 17, 17}}, {'G', {14, 17, 16, 23, 17, 17, 15}}, {'S', {15, 16, 16, 14, 1, 1, 30}},
        {'H', {17, 17, 17, 31, 17, 17, 17}}, {'A', {14, 17, 17, 31, 17, 17, 17}}, {'D', {30, 17, 17, 17, 17, 17, 30}},
        {'E', {31, 16, 16, 30, 16, 16, 31}}, {'R', {30, 17, 17, 30, 20, 18, 17}}, {'0', {14, 17, 19, 21, 25, 17, 14}},
        {'1', {4, 12, 4, 4, 4, 4, 14}},      {'2', {14, 17, 1, 2, 4, 8, 31}},     {'3', {30, 1, 1, 14, 1, 1, 30}},
        {'4', {2, 6, 10, 18, 31, 2, 2}},     {'5', {31, 16, 30, 1, 1, 17, 14}},   {'6', {6, 8, 16, 30, 17, 17, 14}},
        {'7', {31, 1, 2, 4, 8, 8, 8}},       {'8', {14, 17, 17, 14, 17, 17, 14}}, {'9', {14, 17, 17, 15, 1, 2, 12}},
        {'%', {25, 26, 2, 4, 8, 11, 19}},    {'.', {0, 0, 0, 0, 0, 12, 12}},   {'T', {31, 4, 4, 4, 4, 4, 4}},
        {'F', {31, 16, 16, 30, 16, 16, 16}}, {'U', {17, 17, 17, 17, 17, 17, 14}}, {'X', {17, 17, 10, 4, 10, 17, 17}},
        {'Y', {17, 17, 10, 4, 4, 4, 4}},     {'+', {0, 4, 4, 31, 4, 4, 0}},       {'-', {0, 0, 0, 31, 0, 0, 0}},
    };
    for (const auto& g : font)
        if (g.c == c) return g.rows;
    return nullptr;
}

bool createOverlay(App& a) {
    Swapchain& s = a.overlay;
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    ci.format = GL_RGBA8;
    ci.sampleCount = 1;
    ci.width = kOverlayW;
    ci.height = kOverlayH;
    ci.faceCount = ci.arraySize = ci.mipCount = 1;
    if (!xrOk("xrCreateSwapchain(overlay)", xrCreateSwapchain(a.session, &ci, &s.handle))) return false;
    s.width = kOverlayW;
    s.height = kOverlayH;
    uint32_t ic = 0;
    xrEnumerateSwapchainImages(s.handle, 0, &ic, nullptr);
    s.images.assign(ic, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
    xrEnumerateSwapchainImages(s.handle, ic, &ic, reinterpret_cast<XrSwapchainImageBaseHeader*>(s.images.data()));
    s.fbos.resize(ic);
    glGenFramebuffers(static_cast<GLsizei>(ic), s.fbos.data());
    for (uint32_t i = 0; i < ic; ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, s.fbos[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s.images[i].image, 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void drawOverlay(App& a, const char* text, float progress) {
    Swapchain& s = a.overlay;
    uint32_t idx = 0;
    XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    xrAcquireSwapchainImage(s.handle, &ai, &idx);
    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wi.timeout = XR_INFINITE_DURATION;
    xrWaitSwapchainImage(s.handle, &wi);
    glBindFramebuffer(GL_FRAMEBUFFER, s.fbos[idx]);
    glViewport(0, 0, s.width, s.height);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    auto rect = [&](int x, int y, int w, int h, float r, float g, float b) {
        glScissor(x, y, w, h);
        glClearColor(r, g, b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    };
    const int px = 5;  // screen pixels per font pixel
    int len = static_cast<int>(std::strlen(text));
    int x0 = (s.width - len * 6 * px) / 2, top = progress < 0 ? (s.height + 7 * px) / 2 : s.height - 60;
    for (int i = 0; i < len; ++i)
        if (const uint8_t* rows = glyph(text[i]))
            for (int r = 0; r < 7; ++r)
                for (int c = 0; c < 5; ++c)
                    if (rows[r] & (16 >> c)) rect(x0 + (i * 6 + c) * px, top - (r + 1) * px, px, px, 0.92f, 0.92f, 0.92f);
    if (progress >= 0) {  // progress bar
        int bx = 112, bw = s.width - 224, by = 50, bh = 24;
        rect(bx, by, bw, bh, 0.25f, 0.25f, 0.28f);
        rect(bx, by, static_cast<int>(static_cast<float>(bw) * progress), bh, 0.92f, 0.92f, 0.92f);
    }
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(s.handle, &ri);
}

void startEngine(App& a) {
    std::string files = a.android->activity->externalDataPath ? a.android->activity->externalDataPath : "";
    std::string root = dataRoot(files);
    if (chdir(files.c_str()) != 0) LOGE("chdir %s failed", files.c_str());  // saves/ go next to the data
    try {
        a.fs = std::make_unique<PackageFS>(root);
        story::EngineOptions opt;
        opt.width = a.swapchains[0].width;
        opt.height = a.swapchains[0].height;
        opt.msaa = a.msaa;  // default 2: the Steam build's -msaa 2
        opt.policy = a.interpolate ? SamplingPolicy::remaster() : SamplingPolicy::original();
        opt.precompileShaders = false;  // done frame by frame with an overlay (warm-up), see frame()
        opt.shaderCacheDir = files + "/shadercache";
        opt.prefetchHints = root + "/../prefetch_hints.txt";  // learned prefetch, next to the data
        std::filesystem::create_directories(opt.shaderCacheDir);
        a.engine = std::make_unique<story::Engine>(*a.fs, opt);
        story::HmdState& h = a.engine->hmd();
        h.active = true;  // before boot: the camera rig asks DisplayDevice.getType on creation
        h.width = a.swapchains[0].width;
        h.height = a.swapchains[0].height;
        h.position = Vec3(0, 1.2f, 0);
        h.eyeRaise = a.eyeRaise;
        a.engine->renderer().setTextureBudget(kTextureBudget);
        a.engine->time().fixedStepMs = 0;  // real time
        // every shader is built before the story starts (no compiles while it plays); from the
        // binary cache after the first start
        a.engine->renderer().beginPrecompile();
        a.warmup = true;
        if (!a.overlay.handle) createOverlay(a);
        LOGI("engine created, data %s; preparing %zu shaders", root.c_str(), a.engine->renderer().precompileTotal());
    } catch (const std::exception& e) {
        LOGE("engine start failed: %s (Pearl data expected in %s)", e.what(), root.c_str());
        a.engine.reset();
        a.engineFailed = true;
    }
}

// From here on the GL context belongs to a render thread (render/gl_thread.h): the engine
// thread records the GL calls of frame N+1 while the render thread replays frame N, so the
// driver work runs on its own core. /sdcard/Oyster/singlethread keeps everything on one thread.
void startRenderThread(App& a) {
    if (access((std::string(kSharedRoot) + "/singlethread").c_str(), F_OK) == 0) {
        LOGI("render thread disabled (singlethread flag file)");
        return;
    }
    EGLDisplay d = a.display;
    EGLSurface s = a.surface;
    EGLContext c = a.context;
    eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    gl::threaded::start(
        [d, s, c] {
            if (!eglMakeCurrent(d, s, s, c)) LOGE("render thread: eglMakeCurrent failed 0x%x", eglGetError());
            pthread_setname_np(pthread_self(), "OysterRender");
        },
        [d] { eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT); });
    LOGI("render thread started");
}

void bootStory(App& a) {
    try {
        if (!a.engine->boot()) throw std::runtime_error("story boot failed: " + a.engine->lua().lastError());
        LOGI("story booted (%zu shaders from the binary cache)", a.engine->renderer().programsFromCache);
        AAudioStreamBuilder* b = nullptr;
        if (AAudio_createStreamBuilder(&b) == AAUDIO_OK) {
            AAudioStreamBuilder_setSampleRate(b, audio::Engine::kRate);
            AAudioStreamBuilder_setChannelCount(b, 2);
            AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
            AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
            AAudioStreamBuilder_setDataCallback(b, audioCallback, &a.engine->audio());
            if (AAudioStreamBuilder_openStream(b, &a.audio) != AAUDIO_OK) a.audio = nullptr;
            AAudioStreamBuilder_delete(b);
        }
        if (!a.audio) LOGE("AAudio stream could not be opened");
    } catch (const std::exception& e) {
        LOGE("story start failed: %s", e.what());
        a.engine.reset();
        a.engineFailed = true;
    }
}

void handleXrEvents(App& a) {
    XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(a.instance, &ev) == XR_SUCCESS) {
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            auto* sc = reinterpret_cast<XrEventDataSessionStateChanged*>(&ev);
            a.state = sc->state;
            LOGI("session state %d", static_cast<int>(a.state));
            if (a.state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
                bi.primaryViewConfigurationType = kViewConfig;
                a.running = xrOk("xrBeginSession", xrBeginSession(a.session, &bi));
            } else if (a.state == XR_SESSION_STATE_STOPPING) {
                gl::threaded::finish();
                xrEndSession(a.session);
                a.running = false;
            } else if (a.state == XR_SESSION_STATE_EXITING || a.state == XR_SESSION_STATE_LOSS_PENDING) {
                a.android->destroyRequested = 1;
            }
        }
        ev = {XR_TYPE_EVENT_DATA_BUFFER};
    }
}

// Head pose and per-eye field of view for the engine; story time step from the display times.
double updateHead(App& a, const XrFrameState& fs, const XrView views[2], bool focused) {
    story::HmdState& h = a.engine->hmd();
    const XrVector3f &p0 = views[0].pose.position, &p1 = views[1].pose.position;
    h.position = Vec3((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f, (p0.z + p1.z) * 0.5f);
    const XrQuaternionf& q = views[0].pose.orientation;
    h.orientation = Quat(q.x, q.y, q.z, q.w);
    float dx = p1.x - p0.x, dy = p1.y - p0.y, dz = p1.z - p0.z;
    float ipd = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (ipd > 0.04f && ipd < 0.09f) h.ipd = ipd;
    for (int e = 0; e < 2; ++e) {
        h.eye[e].tanLeft = std::tan(views[e].fov.angleLeft);
        h.eye[e].tanRight = std::tan(views[e].fov.angleRight);
        h.eye[e].tanUp = std::tan(views[e].fov.angleUp);
        h.eye[e].tanDown = std::tan(views[e].fov.angleDown);
    }
    // story time runs only while the user is in the experience (focused)
    double dt = 0;
    if (focused && a.lastDisplayTime != 0)
        dt = std::min(0.1, std::max(0.0, static_cast<double>(fs.predictedDisplayTime - a.lastDisplayTime) * 1e-9));
    a.lastDisplayTime = focused ? fs.predictedDisplayTime : 0;
    return dt;
}

// What the render thread needs to finish a frame (captured on the engine thread).
struct FrameOut {
    XrTime displayTime = 0;
    bool render = false;
    XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
    GLuint srcFbo[2] = {};
    int srcW[2] = {}, srcH[2] = {};
    std::string notice;  // head-locked panel text (empty: none)
};

// Render thread: eye images into the swapchains, then xrEndFrame.
void submitFrame(App& a, const FrameOut& o) {
    XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};
    ei.displayTime = o.displayTime;
    ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    XrCompositionLayerProjectionView pv[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
                                             {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    const XrCompositionLayerBaseHeader* layers[] = {reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer),
                                                    reinterpret_cast<XrCompositionLayerBaseHeader*>(&quad)};
    if (o.render) {
        for (int e = 0; e < 2; ++e) {
            Swapchain& s = a.swapchains[e];
            uint32_t idx = 0;
            XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            xrAcquireSwapchainImage(s.handle, &ai, &idx);
            XrSwapchainImageWaitInfo wi2{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            wi2.timeout = XR_INFINITE_DURATION;
            xrWaitSwapchainImage(s.handle, &wi2);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s.fbos[idx]);
            if (a.srgbWriteControl) glDisable(GL_FRAMEBUFFER_SRGB_EXT);
            glDisable(GL_SCISSOR_TEST);  // blits honour the scissor test
            if (o.srcFbo[e]) {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, o.srcFbo[e]);
                glBlitFramebuffer(0, 0, o.srcW[e], o.srcH[e], 0, 0, s.width, s.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            xrReleaseSwapchainImage(s.handle, &ri);
            pv[e].pose = o.views[e].pose;
            pv[e].fov = o.views[e].fov;
            pv[e].subImage.swapchain = s.handle;
            pv[e].subImage.imageRect.extent = {s.width, s.height};
        }
        layer.space = a.space;
        layer.viewCount = 2;
        layer.views = pv;
        ei.layerCount = 1;
        ei.layers = layers;
        if (!o.notice.empty() && a.overlay.handle && a.viewSpace) {
            drawOverlay(a, o.notice.c_str(), -1.0f);
            quad.space = a.viewSpace;
            quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            quad.subImage.swapchain = a.overlay.handle;
            quad.subImage.imageRect.extent = {a.overlay.width, a.overlay.height};
            quad.pose.orientation.w = 1;
            quad.pose.position = {0.0f, -0.25f, -1.5f};
            quad.size = {0.8f, 0.2f};
            ei.layerCount = 2;
        }
    }
    xrEndFrame(a.session, &ei);
}

// Frame with the render thread: the engine thread waits for the frame slot, begins the frame on
// the render thread right away (so the next xrWaitFrame can return while this frame is drawn),
// runs the story and records its GL calls; the render thread replays them and submits.
void frameThreaded(App& a) {
    XrFrameState fs{XR_TYPE_FRAME_STATE};
    XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
    if (!xrOk("xrWaitFrame", xrWaitFrame(a.session, &wi, &fs))) return;
    XrSession session = a.session;
    gl::threaded::enqueue([session] {
        XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
        xrOk("xrBeginFrame", xrBeginFrame(session, &bi));
    });
    gl::threaded::flush();
    auto out = std::make_shared<FrameOut>();
    out->displayTime = fs.predictedDisplayTime;
    bool focused = a.state == XR_SESSION_STATE_FOCUSED;
    setAudio(a, focused && a.engine);
    if (focused) pollActions(a, fs.predictedDisplayTime);
    if (fs.predictedDisplayTime < a.noticeUntil) out->notice = a.notice;
    double frameMs = 0;
    if (fs.shouldRender && a.engine) {
        XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
        li.viewConfigurationType = kViewConfig;
        li.displayTime = fs.predictedDisplayTime;
        li.space = a.space;
        XrViewState vs{XR_TYPE_VIEW_STATE};
        uint32_t vc = 0;
        if (xrOk("xrLocateViews", xrLocateViews(a.session, &li, &vs, 2, &vc, out->views)) && vc == 2) {
            double dt = updateHead(a, fs, out->views, focused);
            auto tf0 = std::chrono::steady_clock::now();
            if (!a.engine->frame(dt)) LOGE("frame %llu: Lua error", static_cast<unsigned long long>(a.frames));
            frameMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tf0).count();
            for (int e = 0; e < 2; ++e)
                if (const RenderTarget* src = a.engine->eyeOutput(e)) {
                    out->srcFbo[e] = src->fbo;
                    out->srcW[e] = src->width;
                    out->srcH[e] = src->height;
                }
            out->render = true;
            if (a.engine->exitRequested) ANativeActivity_finish(a.android->activity);
        }
    }
    App* ap = &a;
    gl::threaded::enqueue([ap, out] { submitFrame(*ap, *out); });
    gl::threaded::endFrame(1);
    if (!out->render) return;
    Renderer& rr = a.engine->renderer();
    if (frameMs > 25.0) LOGI("slow frame %llu: engine thread %.1f ms", static_cast<unsigned long long>(a.frames), frameMs);
    a.statMs += frameMs;
    a.statMax = std::max(a.statMax, frameMs);
    ++a.frames;
    if (++a.statFrames == 360) {
        gl::threaded::Stats t = gl::threaded::stats();
        double n = static_cast<double>(std::max<uint64_t>(1, t.frames - a.statThread.frames));
        LOGI("perf: engine thread avg %.2f ms max %.2f ms; render thread %.2f ms/frame, engine waited %.2f ms/frame, "
             "%llu sync calls; textures read %.0f decode %.0f upload %.0f ms (%zu loads), resident %.0f MB",
             a.statMs / a.statFrames, a.statMax, (t.replayMs - a.statThread.replayMs) / n,
             (t.waitMs - a.statThread.waitMs) / n, static_cast<unsigned long long>(t.syncCalls - a.statThread.syncCalls),
             rr.texReadMs, rr.texDecodeMs, rr.texUploadMs, rr.texLoads, static_cast<double>(rr.textureBytes()) / 1048576.0);
        a.statThread = t;
        a.statMs = a.statMax = 0;
        a.statFrames = 0;
        rr.texReadMs = rr.texDecodeMs = rr.texUploadMs = 0;
        rr.texLoads = 0;
    }
}

void frame(App& a) {
    if (gl::threaded::active()) return frameThreaded(a);
    XrFrameState fs{XR_TYPE_FRAME_STATE};
    XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
    if (!xrOk("xrWaitFrame", xrWaitFrame(a.session, &wi, &fs))) return;
    XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
    if (!xrOk("xrBeginFrame", xrBeginFrame(a.session, &bi))) return;
    XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};
    ei.displayTime = fs.predictedDisplayTime;
    ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;

    XrCompositionLayerProjectionView pv[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
                                             {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    const XrCompositionLayerBaseHeader* layers[] = {reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer)};
    bool focused = a.state == XR_SESSION_STATE_FOCUSED;

    if (a.engine && a.warmup) {
        // shader warm-up: about 8 ms of compiling per frame, progress on a head-locked panel
        bool done = a.engine->renderer().precompileStep(8.0);
        XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
        const XrCompositionLayerBaseHeader* ql[] = {reinterpret_cast<XrCompositionLayerBaseHeader*>(&quad)};
        if (fs.shouldRender && a.overlay.handle && a.viewSpace) {
            size_t total = std::max<size_t>(1, a.engine->renderer().precompileTotal());
            float progress = static_cast<float>(a.engine->renderer().precompileDone()) / static_cast<float>(total);
            char text[48];
            std::snprintf(text, sizeof(text), "COMPILING SHADERS %d%%", static_cast<int>(progress * 100.0f + 0.5f));
            drawOverlay(a, text, progress);
            quad.space = a.viewSpace;
            quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            quad.subImage.swapchain = a.overlay.handle;
            quad.subImage.imageRect.extent = {a.overlay.width, a.overlay.height};
            quad.pose.orientation.w = 1;
            quad.pose.position = {0.0f, 0.0f, -1.5f};
            quad.size = {0.8f, 0.2f};
            ei.layerCount = 1;
            ei.layers = ql;
        }
        xrEndFrame(a.session, &ei);
        if (done) {
            a.warmup = false;
            LOGI("shaders ready");
            startRenderThread(a);
            bootStory(a);
        }
        return;
    }
    setAudio(a, focused && a.engine);

    if (fs.shouldRender && a.engine) {
        XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
        XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
        li.viewConfigurationType = kViewConfig;
        li.displayTime = fs.predictedDisplayTime;
        li.space = a.space;
        XrViewState vs{XR_TYPE_VIEW_STATE};
        uint32_t vc = 0;
        if (xrOk("xrLocateViews", xrLocateViews(a.session, &li, &vs, 2, &vc, views)) && vc == 2) {
            // head = midpoint of the eyes, orientation of the head (both eyes share it on Quest)
            story::HmdState& h = a.engine->hmd();
            const XrVector3f &p0 = views[0].pose.position, &p1 = views[1].pose.position;
            h.position = Vec3((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f, (p0.z + p1.z) * 0.5f);
            const XrQuaternionf& q = views[0].pose.orientation;
            h.orientation = Quat(q.x, q.y, q.z, q.w);
            float dx = p1.x - p0.x, dy = p1.y - p0.y, dz = p1.z - p0.z;
            float ipd = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (ipd > 0.04f && ipd < 0.09f) h.ipd = ipd;
            for (int e = 0; e < 2; ++e) {
                h.eye[e].tanLeft = std::tan(views[e].fov.angleLeft);
                h.eye[e].tanRight = std::tan(views[e].fov.angleRight);
                h.eye[e].tanUp = std::tan(views[e].fov.angleUp);
                h.eye[e].tanDown = std::tan(views[e].fov.angleDown);
            }
            // story time runs only while the user is in the experience (focused)
            double dt = 0;
            if (focused && a.lastDisplayTime != 0)
                dt = std::min(0.1, std::max(0.0, static_cast<double>(fs.predictedDisplayTime - a.lastDisplayTime) * 1e-9));
            a.lastDisplayTime = focused ? fs.predictedDisplayTime : 0;
            Renderer& rr = a.engine->renderer();
            double texBefore = rr.texReadMs + rr.texDecodeMs + rr.texUploadMs;
            size_t loadsBefore = rr.texLoads;
            auto tf0 = std::chrono::steady_clock::now();
            if (!a.engine->frame(dt)) LOGE("frame %llu: Lua error", static_cast<unsigned long long>(a.frames));
            double frameMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tf0).count();
            double texMs = rr.texReadMs + rr.texDecodeMs + rr.texUploadMs - texBefore;
            if (frameMs > 25.0)
                LOGI("slow frame %llu: %.1f ms (%zu texture loads %.1f ms)", static_cast<unsigned long long>(a.frames),
                     frameMs, rr.texLoads - loadsBefore, texMs);
            a.statMs += frameMs;
            a.statMax = std::max(a.statMax, frameMs);
            if (++a.statFrames == 360) {
                LOGI("perf: engine frame avg %.2f ms max %.2f ms; textures read %.0f decode %.0f upload %.0f ms (%zu loads), "
                     "resident %.0f MB",
                     a.statMs / a.statFrames, a.statMax, rr.texReadMs, rr.texDecodeMs, rr.texUploadMs, rr.texLoads,
                     static_cast<double>(rr.textureBytes()) / 1048576.0);
                a.statMs = a.statMax = 0;
                a.statFrames = 0;
                rr.texReadMs = rr.texDecodeMs = rr.texUploadMs = 0;
                rr.texLoads = 0;
            }
            ++a.frames;
            for (int e = 0; e < 2; ++e) {
                Swapchain& s = a.swapchains[e];
                uint32_t idx = 0;
                XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                xrAcquireSwapchainImage(s.handle, &ai, &idx);
                XrSwapchainImageWaitInfo wi2{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                wi2.timeout = XR_INFINITE_DURATION;
                xrWaitSwapchainImage(s.handle, &wi2);
                const RenderTarget* src = a.engine->eyeOutput(e);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s.fbos[idx]);
                if (a.srgbWriteControl) glDisable(GL_FRAMEBUFFER_SRGB_EXT);
                glDisable(GL_SCISSOR_TEST);  // blits honour the scissor test
                if (src) {
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, src->fbo);
                    glBlitFramebuffer(0, 0, src->width, src->height, 0, 0, s.width, s.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
                }
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                xrReleaseSwapchainImage(s.handle, &ri);
                pv[e].pose = views[e].pose;
                pv[e].fov = views[e].fov;
                pv[e].subImage.swapchain = s.handle;
                pv[e].subImage.imageRect.extent = {s.width, s.height};
            }
            layer.space = a.space;
            layer.viewCount = 2;
            layer.views = pv;
            ei.layerCount = 1;
            ei.layers = layers;
            if (a.engine->exitRequested) ANativeActivity_finish(a.android->activity);
        }
    }
    xrEndFrame(a.session, &ei);
}

// Self test without an OpenXR session (nobody needs to wear the headset): if the file
// <files>/selftest exists (content: number of frames, default 3000), the story runs offscreen
// with a seated, still head at the Quest 3's eye size and field of view, at a fixed 72 Hz step;
// CPU+GPU time per frame (glFinish) and texture loading are logged, then the app exits. The
// flag file is removed so the next launch is normal.
bool selfTest(App& a) {
    std::string files = a.android->activity->externalDataPath ? a.android->activity->externalDataPath : "";
    std::string flag = std::string(kSharedRoot) + "/selftest";
    FILE* f = std::fopen(flag.c_str(), "r");
    if (!f) return false;
    int frames = 3000;
    if (std::fscanf(f, "%d", &frames) != 1 || frames <= 0) frames = 3000;
    std::fclose(f);
    std::remove(flag.c_str());
    LOGI("self test: %d frames", frames);
    if (!initEgl(a)) {
        LOGE("self test: EGL failed");
        return true;
    }
    if (chdir(files.c_str()) != 0) LOGE("chdir %s failed", files.c_str());
    try {
        PackageFS fs(dataRoot(files));
        story::EngineOptions opt;
        opt.width = 1680;
        opt.height = 1760;
        opt.msaa = 2;
        story::Engine engine(fs, opt);
        story::HmdState& h = engine.hmd();
        h.active = true;
        h.width = 1680;
        h.height = 1760;
        h.position = Vec3(0, 1.2f, 0);
        h.eye[0] = {-1.13f, 0.84f, 0.90f, -1.13f};
        h.eye[1] = {-0.84f, 1.13f, 0.90f, -1.13f};
        engine.renderer().setTextureBudget(kTextureBudget);
        engine.time().fixedStepMs = 1000.0 / 72.0;
        auto tb = std::chrono::steady_clock::now();
        if (!engine.boot()) throw std::runtime_error("story boot failed: " + engine.lua().lastError());
        LOGI("self test: story booted in %.0f ms",
             std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tb).count());
        Renderer& rr = engine.renderer();
        double sum = 0, mx = 0, all = 0;
        int n = 0, over = 0;
        for (int i = 0; i < frames && !engine.exitRequested; ++i) {
            size_t loads = rr.texLoads;
            double tex = rr.texReadMs + rr.texDecodeMs + rr.texUploadMs;
            auto t0 = std::chrono::steady_clock::now();
            engine.frame(0);
            glFinish();
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (ms > 25.0)
                LOGI("slow frame %d: %.1f ms (%zu texture loads %.1f ms)", i, ms, rr.texLoads - loads,
                     rr.texReadMs + rr.texDecodeMs + rr.texUploadMs - tex);
            sum += ms;
            all += ms;
            mx = std::max(mx, ms);
            over += ms > 13.9 ? 1 : 0;
            if (++n == 360) {
                LOGI("self test frames %d-%d: avg %.2f ms max %.2f ms, %d over 13.9 ms; textures read %.0f decode %.0f "
                     "upload %.0f ms (%zu loads), resident %.0f MB",
                     i - 359, i, sum / n, mx, over, rr.texReadMs, rr.texDecodeMs, rr.texUploadMs, rr.texLoads,
                     static_cast<double>(rr.textureBytes()) / 1048576.0);
                sum = mx = 0;
                n = over = 0;
                rr.texReadMs = rr.texDecodeMs = rr.texUploadMs = 0;
                rr.texLoads = 0;
            }
        }
        LOGI("self test done: %llu frames, story time %.1f s, average %.2f ms per frame",
             static_cast<unsigned long long>(engine.frameIndex), static_cast<double>(engine.time().elapsedUs) * 1e-6,
             all / static_cast<double>(std::max<uint64_t>(1, engine.frameIndex)));
    } catch (const std::exception& e) {
        LOGE("self test failed: %s", e.what());
    }
    return true;
}

void onAppCmd(android_app* app, int32_t cmd) {
    App& a = *static_cast<App*>(app->userData);
    if (cmd == APP_CMD_RESUME) a.resumed = true;
    else if (cmd == APP_CMD_PAUSE) a.resumed = false;
    else if (cmd == APP_CMD_INIT_WINDOW && a.instance == XR_NULL_HANDLE) {
        if (!initXr(a)) LOGE("OpenXR initialisation failed");
    }
}

}  // namespace

void android_main(android_app* app) {
    redirectStdio();
    setenv("OYSTER_DEBUG_TIMING", "1", 1);  // engine CPU breakdown every 300 frames in the log
    App a;
    a.android = app;
    app->userData = &a;
    app->onAppCmd = onAppCmd;
    if (selfTest(a)) {
        app->onAppCmd = nullptr;  // no OpenXR session for the self test
        ANativeActivity_finish(app->activity);
        while (!app->destroyRequested) {
            android_poll_source* src = nullptr;
            if (ALooper_pollOnce(-1, nullptr, nullptr, reinterpret_cast<void**>(&src)) >= 0 && src) src->process(app, src);
        }
        return;
    }
    while (!app->destroyRequested) {
        for (;;) {
            android_poll_source* src = nullptr;
            int timeout = (!a.resumed && !a.running) ? -1 : 0;
            if (ALooper_pollOnce(timeout, nullptr, nullptr, reinterpret_cast<void**>(&src)) < 0) break;
            if (src) src->process(app, src);
            if (app->destroyRequested) break;
        }
        if (a.instance == XR_NULL_HANDLE) continue;
        handleXrEvents(a);
        if (!a.running) continue;
        if (!a.engine && !a.engineFailed) startEngine(a);
        frame(a);
    }
    if (a.audio) {
        AAudioStream_requestStop(a.audio);
        AAudioStream_close(a.audio);
    }
    a.engine.reset();
    gl::threaded::stop();  // replays the engine's GL deletes, releases the context
    if (a.session != XR_NULL_HANDLE) xrDestroySession(a.session);
    if (a.instance != XR_NULL_HANDLE) xrDestroyInstance(a.instance);
}
