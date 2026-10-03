#include "audio/audio_engine.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include <vorbis/vorbisfile.h>

namespace oyster::audio {

namespace {

constexpr int kSpeakers = 4;

// Balance pan table of FUN_1800446c0 (pan mode 1, the default): 1.0 for indices 0..511, then
// falling linearly to 0 in steps of 1/512. Left uses table[i], right table[1023 - i].
struct PanTable {
    float t[1024];
    PanTable() {
        for (uint32_t i = 0; i < 0x200; ++i) t[0x3ff - i] = static_cast<float>(i) * 0.001953125f;
        for (uint32_t i = 0x200; i < 0x400; ++i) t[0x3ff - i] = 1.0f;
    }
};
const PanTable kPan;

inline int16_t sat(int v) {  // engine clamp: [-32767, 32767]
    if (0x7fff < v) v = 0x7fff;
    if (v < -0x7fff) v = -0x7fff;
    return static_cast<int16_t>(v);
}

// Emitter2dEffect parameter table (0x18030a1f0): min, max
const float kParamMin[11] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
const float kParamMax[11] = {1, FLT_MAX, FLT_MAX, 1, 1, 1, 180, 180, 1, 1, 1};
const float kParamDefault[11] = {1.0f, 1.0f, 50.0f, 1.0f, 0.0f, 0.0f, 1.5707963705f, 3.1415927410f, 1.0f, 0.25f, 0.0f};
const float kDegToRad = 0.01745329238474369f;

// AmbisonicFirstOrderStereoEffect: 48 kHz shelf crossover (FUN_180040600) - high-pass and
// low-pass biquads sharing the poles; out = LP(x) - HP(x) * k, k = sqrt2 (W) / sqrt(2/3) (X,Y,Z)
const float kHP[5] = {0.91424733400345f, -1.82849466800690f, 0.91424733400345f, -1.82465136051178f, 0.83233809471130f};
const float kLP[5] = {0.00192169775255f, 0.00384339550510f, 0.00192169775255f, -1.82465136051178f, 0.83233809471130f};
const float kShelfW = 1.414164662361145f;
const float kShelfXYZ = 0.8164966106414795f;
const float kInt16ToFloat = 3.0518509447574615e-05f;
// virtual speaker decode (0x1802319b0): mid = a*W + b*X + d*Z, side = c*Y
const float kDecode[kSpeakers][4] = {{0.1768f, 0.25f, -0.25f, -0.1768f},
                                     {0.1768f, 0.25f, -0.25f, 0.1768f},
                                     {0.1768f, -0.25f, -0.25f, -0.1768f},
                                     {0.1768f, -0.25f, -0.25f, 0.1768f}};
// speaker directions (0x1803047b0, azimuth negated when used): (az, el)
const float kSpeakerDir[kSpeakers][2] = {{-45.0f, -45.0f}, {-45.0f, 45.0f}, {-135.0f, -45.0f}, {-135.0f, 45.0f}};

struct Biquad {
    float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    float run(float x) {
        float y = ((b0 * x + b1 * x1 + b2 * x2) - y1 * a1) - y2 * a2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
};

// listener forward = rotation applied to (0, 0, -1), in the engine's operation order
Vec3 listenerForward(const Quat& q) {
    const float K = -1.0f;
    float cx = q.y * K - q.z * 0.0f;
    float cy = q.z * 0.0f - q.x * K;
    float cz = q.x * 0.0f - q.y * 0.0f;
    float dy = cx * q.z - cz * q.x;
    float dx = cz * q.y - cy * q.z;
    float dz = cy * q.x - cx * q.y;
    float w2 = q.w + q.w;
    return {cx * w2 + dx + dx, cy * w2 + dy + dy, (cz * w2 - 1.0f) + dz + dz};
}

}  // namespace

std::shared_ptr<PcmClip> decodeVorbis(const std::vector<uint8_t>& file) {
    struct Mem { const std::vector<uint8_t>* d; size_t pos; } mem{&file, 0};
    ov_callbacks cb;
    cb.read_func = [](void* ptr, size_t size, size_t n, void* src) -> size_t {
        auto* m = static_cast<Mem*>(src);
        size_t want = size * n, left = m->d->size() - m->pos;
        size_t take = std::min(want, left);
        std::memcpy(ptr, m->d->data() + m->pos, take);
        m->pos += take;
        return size ? take / size : 0;
    };
    cb.seek_func = [](void* src, ogg_int64_t off, int whence) -> int {
        auto* m = static_cast<Mem*>(src);
        int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? static_cast<int64_t>(m->pos) : static_cast<int64_t>(m->d->size());
        int64_t p = base + off;
        if (p < 0 || p > static_cast<int64_t>(m->d->size())) return -1;
        m->pos = static_cast<size_t>(p);
        return 0;
    };
    cb.close_func = nullptr;
    cb.tell_func = [](void* src) -> long { return static_cast<long>(static_cast<Mem*>(src)->pos); };
    OggVorbis_File vf;
    if (ov_open_callbacks(&mem, &vf, nullptr, 0, cb) != 0) throw std::runtime_error("not an Ogg Vorbis stream");
    vorbis_info* vi = ov_info(&vf, -1);
    auto clip = std::make_shared<PcmClip>();
    clip->channels = vi->channels;
    clip->rate = static_cast<int>(vi->rate);
    char buf[16384];
    int section = 0;
    for (;;) {
        long n = ov_read(&vf, buf, sizeof(buf), 0, 2, 1, &section);
        if (n <= 0) break;
        size_t old = clip->data.size();
        clip->data.resize(old + static_cast<size_t>(n) / 2);
        std::memcpy(clip->data.data() + old, buf, static_cast<size_t>(n));
    }
    ov_clear(&vf);
    clip->frames = static_cast<uint32_t>(clip->data.size() / static_cast<size_t>(clip->channels));
    return clip;
}

// ---------------------------------------------------------------------------------------------

struct Engine::Player {
    std::shared_ptr<const PcmClip> clip;
    int state = 0;  // 0 stopped, 2 playing
    uint32_t pos = 0;
    bool loop = false;
    int emitter = 0;
    // AudPlayer +0x44.. volume ramp, +0x60.. pan ramp
    float vol = 1.0f, volTarget = 1.0f, volReq = 1.0f, volStep = 0.0f;
    uint32_t volRemain = 0, volReqFrames = 0;
    bool volPending = false;
    float pan = 0.5f, panTarget = 0.5f, panReq = 0.5f, panStep = 0.0f;
    uint32_t panRemain = 0, panReqFrames = 0;
    bool panPending = false;
};

struct Engine::Emitter {
    int type = 1;  // engine MEMITTERTYPE: 1 = 2D, 2 = 3D, 3 = surround
    Vec3 pos;
    Quat rot;
    std::vector<int16_t> buf;  // prespatial: stereo (2D) or 4 channels (surround)
    bool used = false;         // a player mixed into it this block (+0x1f8)
    // Emitter2dEffect
    float param[11];
    float panPrev = 0.5f, panTarget = 0.5f, gainPrev = 1.0f, gainTarget = 1.0f;
    // AmbisonicFirstOrderStereoEffect
    Biquad hp[4], lp[4];
    float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::vector<float> scratch;  // 4 per frame
};

// BiquadBinauralFilter: delay line (allpass fraction) + 2 x (gain, 6 biquads)
struct Engine::Speaker {
    float gain[2] = {1, 1};
    float coef[2][6][5] = {};  // b0 b1 b2 a1 a2
    float st[2][7][3] = {};    // per stage input history (x0, x1, x2) - stage outputs feed the next
    float y[2][6][2] = {};     // per stage y1, y2
    std::vector<float> line[2];
    int size = 481, wr[2] = {0, 0}, rd[2] = {0, 0};
    float frac[2] = {0, 0}, ap[2] = {0, 0}, prevIn[2] = {0, 0}, prevOut[2] = {0, 0};

    void setRecord(const float* rec) {
        for (int c = 0; c < 2; ++c) {
            const float* r = rec + 3 + 31 * c;
            gain[c] = r[0];
            for (int s = 0; s < 6; ++s)
                for (int k = 0; k < 5; ++k) coef[c][s][k] = r[1 + 5 * s + k];
            line[c].assign(static_cast<size_t>(size), 0.0f);
        }
        // FUN_180007c30: delay = fs * rec[2] ms
        float d = rec[2] * 0.001f;
        for (int c = 0; c < 2; ++c) {
            float samples = static_cast<float>(Engine::kRate) * d;
            float ip = static_cast<float>(static_cast<int64_t>(samples));
            int r = wr[c] - static_cast<int>(ip);
            if (r < 0) r += size;
            rd[c] = r;
            frac[c] = samples - static_cast<float>(static_cast<int>(ip));
            if (frac[c] != 0.0f) ap[c] = (1.0f - frac[c]) / (frac[c] + 1.0f);
        }
    }
    // FUN_1800066d0 + FUN_1800076c0 for one frame
    void process(const float in[2], float out[2]) {
        float x[2];
        for (int c = 0; c < 2; ++c) {
            line[c][static_cast<size_t>(wr[c])] = in[c];
            float v = line[c][static_cast<size_t>(rd[c])];
            if (frac[c] != 0.0f) {
                float o = (v - prevOut[c]) * ap[c] + prevIn[c];
                prevIn[c] = v;
                prevOut[c] = o;
                v = o;
            }
            x[c] = v;
            wr[c] = (wr[c] + 1) % size;
            rd[c] = (rd[c] + 1) % size;
        }
        for (int c = 0; c < 2; ++c) {
            float v = x[c];
            float* xi = st[c][0];
            xi[0] = v;
            for (int s = 0; s < 6; ++s) {
                const float* k = coef[c][s];
                float* xs = st[c][s];
                float yv = ((k[1] * xs[1] + k[0] * xs[0] + k[2] * xs[2]) - y[c][s][0] * k[3]) - y[c][s][1] * k[4];
                st[c][s + 1][0] = yv;
                // history shift of this stage's input
                xs[2] = xs[1];
                xs[1] = xs[0];
                y[c][s][1] = y[c][s][0];
                y[c][s][0] = yv;
                v = yv;
            }
            out[c] = gain[c] * v;
        }
    }
};

Engine::Engine() = default;
Engine::~Engine() = default;

bool Engine::loadHrtf(const std::vector<uint8_t>& file) {
    // AudServices::setHRTF: "HRTF" magic + version, then 0x104-byte records (count = size / 0x104)
    if (file.size() < 8 + 0x104) return false;
    size_t count = file.size() / 0x104;
    size_t base = std::memcmp(file.data(), "HRTF", 4) == 0 ? 8 : 0;
    std::vector<std::vector<float>> recs;
    for (size_t i = 0; i < count && base + (i + 1) * 0x104 <= file.size(); ++i) {
        std::vector<float> r(65);
        std::memcpy(r.data(), file.data() + base + i * 0x104, 0x104);
        recs.push_back(std::move(r));
    }
    std::lock_guard<std::mutex> g(mutex_);
    speakers_.clear();
    for (int k = 0; k < kSpeakers; ++k) {
        // BiquadBinauralFilter slot 0: nearest record to (-az, el) (k-d tree on (az, el))
        float az = -kSpeakerDir[k][0], el = kSpeakerDir[k][1];
        size_t best = 0;
        float bestD = FLT_MAX;
        for (size_t i = 0; i < recs.size(); ++i) {
            float da = az - recs[i][0], de = el - recs[i][1];
            float dd = da * da + de * de;
            if (dd < bestD) { bestD = dd; best = i; }
        }
        auto sp = std::make_unique<Speaker>();
        sp->setRecord(recs[best].data());
        speakers_.push_back(std::move(sp));
    }
    speakerBuf_.assign(static_cast<size_t>(kSpeakers) * kBlock * 2, 0.0f);
    return true;
}

int Engine::createEmitter(int luaType) {
    auto e = std::make_unique<Emitter>();
    e->type = luaType == 0 ? 1 : luaType == 1 ? 2 : 3;
    std::copy(kParamDefault, kParamDefault + 11, e->param);
    e->buf.assign(static_cast<size_t>(kBlock) * (e->type == 3 ? 4 : 2), 0);
    if (e->type == 3) {
        for (int c = 0; c < 4; ++c) {
            Biquad h, l;
            h.b0 = kHP[0]; h.b1 = kHP[1]; h.b2 = kHP[2]; h.a1 = kHP[3]; h.a2 = kHP[4];
            l.b0 = kLP[0]; l.b1 = kLP[1]; l.b2 = kLP[2]; l.a1 = kLP[3]; l.a2 = kLP[4];
            e->hp[c] = h;
            e->lp[c] = l;
        }
        e->scratch.assign(static_cast<size_t>(kBlock) * 4, 0.0f);
    }
    std::lock_guard<std::mutex> g(mutex_);
    e->rot = Quat(0, 0, 0, 0);
    emitters_.push_back(std::move(e));
    Emitter& ref = *emitters_.back();
    // listener state is shared by all emitters (AudioSystem forwards it to each)
    if (listenerRot_.x != 0 || listenerRot_.y != 0 || listenerRot_.z != 0 || listenerRot_.w != 0) updateRotation(ref);
    return static_cast<int>(emitters_.size());
}

void Engine::destroyEmitter(int id) {
    std::lock_guard<std::mutex> g(mutex_);
    if (id <= 0 || static_cast<size_t>(id) > emitters_.size()) return;
    emitters_[static_cast<size_t>(id) - 1].reset();
    for (auto& p : players_)
        if (p && p->emitter == id) p->emitter = 0;
}

Engine::Emitter* Engine::emitter(int id) {
    if (id <= 0 || static_cast<size_t>(id) > emitters_.size()) return nullptr;
    return emitters_[static_cast<size_t>(id) - 1].get();
}
Engine::Player* Engine::player(int id) {
    if (id <= 0 || static_cast<size_t>(id) > players_.size()) return nullptr;
    return players_[static_cast<size_t>(id) - 1].get();
}

void Engine::setEmitterPosition(int id, const Vec3& p) {
    std::lock_guard<std::mutex> g(mutex_);
    if (Emitter* e = emitter(id)) e->pos = p;
}
void Engine::setEmitterRotation(int id, const Quat& q) {
    std::lock_guard<std::mutex> g(mutex_);
    if (Emitter* e = emitter(id)) e->rot = q;
}
void Engine::setEmitterParam(int id, int index, float v) {
    // Emitter2dEffect slot +0x20: out-of-range values become the minimum
    std::lock_guard<std::mutex> g(mutex_);
    Emitter* e = emitter(id);
    if (!e || e->type != 1 || index < 0 || index > 10) return;
    float c = kParamMin[index];
    if (kParamMin[index] <= v && v <= kParamMax[index]) c = v;
    if (index == 0) e->param[0] = 0.0f < v ? 1.0f : 0.0f;
    else if (index == 6 || index == 7) e->param[index] = c * kDegToRad;
    else e->param[index] = c;
}
void Engine::setListenerPosition(const Vec3& p) {
    std::lock_guard<std::mutex> g(mutex_);
    listenerPos_ = p;
}
void Engine::setListenerRotation(const Quat& q) {
    std::lock_guard<std::mutex> g(mutex_);
    if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w)) return;
    if (q.x == 0 && q.y == 0 && q.z == 0 && q.w == 0) return;
    if (q.x == listenerRot_.x && q.y == listenerRot_.y && q.z == listenerRot_.z && q.w == listenerRot_.w) return;
    listenerRot_ = q;
    for (auto& e : emitters_)
        if (e && e->type == 3) updateRotation(*e);
}

// FUN_180040800 + Matrix4::setEuler(0, pitch, yaw)
void Engine::updateRotation(Emitter& e) {
    Vec3 f = listenerForward(listenerRot_);
    float len = std::sqrt(f.y * f.y + f.x * f.x + f.z * f.z);
    float ey = -std::asin(-(f.y / len));
    float ez = std::atan2(f.x, -f.z);
    float c1 = 1.0f, s1 = 0.0f;  // x = 0
    float c3 = std::cos(ey), s3 = std::sin(ey);
    float c5 = std::cos(ez), s5 = std::sin(ez);
    e.m[0] = c5 * c3;
    e.m[1] = s3 * s1 - s5 * c3 * c1;
    e.m[2] = s5 * c3 * s1 + s3 * c1;
    e.m[3] = s5;
    e.m[4] = c5 * c1;
    e.m[5] = -(c5 * s1);
    e.m[6] = -(c5 * s3);
    e.m[7] = s5 * s3 * c1 + c3 * s1;
    e.m[8] = c3 * c1 - s5 * s3 * s1;
}

// FUN_18004ac00: target pan / gain of a 2D emitter
void Engine::update2d(Emitter& e) {
    const float* P = e.param;
    Vec3 p = e.pos - listenerPos_;
    float dist = std::sqrt(p.y * p.y + p.x * p.x + p.z * p.z);
    e.gainTarget = 1.0f;
    if (dist <= 0.0f) {
        e.panTarget = 0.5f;
        return;
    }
    const Quat& q = listenerRot_;
    Vec3 f = listenerForward(q);
    float ry = f.y * 0.0f;
    float rx = ry - f.z;                    // fVar9 = r.y*0 - r.z
    float rz = f.x - ry;                    // fVar6
    float rmid = f.z * 0.0f - f.x * 0.0f;   // fVar10
    float rl = std::sqrt(rx * rx + rmid * rmid + rz * rz);
    if (0.0f < rl) {
        float inv = 1.0f / rl;
        rx *= inv; rmid *= inv; rz *= inv;
    }
    float dx = p.x, dzv = p.z, dmid = 0.0f;
    float dl = std::sqrt(dzv * dzv + dx * dx);
    if (0.0f < dl) {
        float inv = 1.0f / dl;
        dx *= inv;
        dmid = inv * 0.0f;
        dzv *= inv;
    }
    float pan = P[0] <= 0.0f ? 0.5f : (rx * dx + rmid * dmid + rz * dzv + 1.0f) * 0.5f;
    if (pan < 0.0f) pan = 0.0f;
    else if (1.0f < pan) pan = 1.0f;
    e.panTarget = pan;
    // distance rolloff
    int rc = static_cast<int>(P[5]);
    if (rc == 0) {
        e.gainTarget = 1.0f;
    } else if (P[1] < dist) {
        if (dist < P[2]) {
            if (rc == 1) e.gainTarget = ((P[4] - P[3]) * (P[2] - dist)) / (P[2] - P[1]) + P[3];
            else if (rc == 3)
                e.gainTarget = static_cast<float>(std::atan(static_cast<double>(((P[2] - dist) * 14.0f) / (P[2] - P[1]) - 7.0f)) *
                                                      0.6366197546520227 + 1.5707963705062866) * (P[4] - P[3]) + P[3];
        } else {
            e.gainTarget = P[3];
        }
    } else {
        e.gainTarget = P[4];
    }
    // angular rolloff between the listener's forward vector and the source direction
    int ac = static_cast<int>(P[10]);
    if (ac != 0) {
        float angle = 0.0f;
        float pl = p.y * p.y + p.x * p.x + p.z * p.z;
        if (0.0f < pl) {
            float fl = f.x * f.x + f.y * f.y + f.z * f.z;
            if (0.0f < fl) {
                float den = std::sqrt(fl) * std::sqrt(pl);
                if (0.0f < den) {
                    float c = (f.x * p.x + p.y * f.y + p.z * f.z) / den;
                    if (1.0f < c) c = 1.0f;
                    else if (c <= -1.0f) c = -1.0f;
                    angle = std::acos(c);
                }
            }
        }
        if (P[6] < angle) {
            if (angle < P[7]) {
                if (ac == 1) e.gainTarget *= ((P[9] - P[8]) * (P[7] - angle)) / (P[7] - P[6]) + P[8];
                else if (ac == 3)
                    e.gainTarget *= static_cast<float>(std::atan(static_cast<double>(((P[7] - angle) * 14.0f) / (P[7] - P[6]) - 7.0f)) *
                                                           0.6366197546520227 + 1.5707963705062866) * (P[9] - P[8]) + P[8];
            } else {
                e.gainTarget *= P[8];
            }
        } else {
            e.gainTarget *= P[9];
        }
    }
}

int Engine::createPlayer(std::shared_ptr<const PcmClip> clip) {
    auto p = std::make_unique<Player>();
    p->clip = std::move(clip);
    std::lock_guard<std::mutex> g(mutex_);
    players_.push_back(std::move(p));
    return static_cast<int>(players_.size());
}
void Engine::destroyPlayer(int id) {
    std::lock_guard<std::mutex> g(mutex_);
    if (id > 0 && static_cast<size_t>(id) <= players_.size()) players_[static_cast<size_t>(id) - 1].reset();
}
void Engine::play(int id, int em) {
    std::lock_guard<std::mutex> g(mutex_);
    Player* p = player(id);
    if (!p) return;
    if (em) p->emitter = em;
    p->state = 2;
}
void Engine::stop(int id) {
    std::lock_guard<std::mutex> g(mutex_);
    if (Player* p = player(id)) {
        p->pos = 0;
        p->state = 0;
    }
}
void Engine::setVolume(int id, float v, uint32_t frames) {
    // AudioSystem::setPlayerVolumeTimed -> AudPlayer::setVolume
    std::lock_guard<std::mutex> g(mutex_);
    Player* p = player(id);
    if (!p || v < 0.0f || 8.0f < v) return;
    if (frames == 0 && p->state != 2) {
        p->volTarget = p->volReq = p->vol = v;
        p->volRemain = 0;
        p->volStep = 0;
        return;
    }
    p->volReq = v;
    p->volReqFrames = frames;
    p->volPending = true;
}
void Engine::setPan(int id, float luaPan, uint32_t frames) {
    // AudioPlayerMaux::setPanOverTime: (pan + 1) / 2 clamped to [0, 1]
    float v = (luaPan + 1.0f) * 0.5f;
    if (v < 0.0f) v = 0.0f;
    else if (1.0f <= v) v = 1.0f;
    std::lock_guard<std::mutex> g(mutex_);
    Player* p = player(id);
    if (!p) return;
    if (frames == 0 && p->state != 2) {
        p->panTarget = p->panReq = p->pan = v;
        p->panRemain = 0;
        p->panStep = 0;
        return;
    }
    p->panReq = v;
    p->panReqFrames = frames;
    p->panPending = true;
}
float Engine::volume(int id) {
    std::lock_guard<std::mutex> g(mutex_);
    Player* p = player(id);
    return p ? p->vol : 0.0f;
}
bool Engine::isPlaying(int id) {
    std::lock_guard<std::mutex> g(mutex_);
    Player* p = player(id);
    return p && p->state == 2;
}
void Engine::setLooping(int id, bool loop) {
    std::lock_guard<std::mutex> g(mutex_);
    if (Player* p = player(id)) p->loop = loop;
}
float Engine::timePosition(int id) {
    std::lock_guard<std::mutex> g(mutex_);
    Player* p = player(id);
    return p && p->clip ? static_cast<float>(p->pos) / static_cast<float>(p->clip->rate) : 0.0f;
}
void Engine::setTimePosition(int id, float s) {
    std::lock_guard<std::mutex> g(mutex_);
    Player* p = player(id);
    if (p && p->clip) p->pos = std::min(p->clip->frames, static_cast<uint32_t>(s * static_cast<float>(p->clip->rate)));
}

// ---------------------------------------------------------------------------------------------
// Mixing kernels (AudPlayer::mixdown targets)

namespace {
// FUN_180044920: mono -> stereo accumulate (double pan index, no gain clamp)
void mixMono(uint32_t n, const int16_t* in, int16_t* out, float g0, float gs, float p0, float ps) {
    for (uint32_t i = 0; i < n; ++i) {
        float s = static_cast<float>(in[i]) * (static_cast<float>(i) * gs + g0);
        uint32_t idx = static_cast<uint32_t>(static_cast<int64_t>(static_cast<double>(static_cast<float>(i) * ps + p0) * 1023.999));
        idx &= 0x3ff;
        out[2 * i] = sat(static_cast<int>(s * kPan.t[idx] + static_cast<float>(out[2 * i])));
        out[2 * i + 1] = sat(static_cast<int>(s * kPan.t[0x3ff - idx] + static_cast<float>(out[2 * i + 1])));
    }
}
// FUN_180044d40: stereo accumulate with balance
void mixStereo(uint32_t n, const int16_t* in, int16_t* out, float g0, float gs, float p0, float ps) {
    float p1023 = p0 * 1023.0f;
    for (uint32_t i = 0; i < n; ++i) {
        float g = static_cast<float>(i) * gs + g0;
        if (1.0f <= g) g = 1.0f;
        uint32_t idx = static_cast<uint32_t>(static_cast<int64_t>(static_cast<float>(i) * ps * 1023.0f + p1023)) & 0x3ff;
        out[2 * i] = sat(static_cast<int>(static_cast<float>(in[2 * i]) * g * kPan.t[idx] + static_cast<float>(out[2 * i])));
        out[2 * i + 1] =
            sat(static_cast<int>(static_cast<float>(in[2 * i + 1]) * g * kPan.t[0x3ff - idx] + static_cast<float>(out[2 * i + 1])));
    }
}
// FUN_180045520: 4 channels into a surround emitter buffer
void mixQuad(uint32_t n, const int16_t* in, int16_t* out, float g0, float gs) {
    for (uint32_t i = 0; i < n; ++i) {
        float g = static_cast<float>(i) * gs + g0;
        if (1.0f <= g) g = 1.0f;
        for (int c = 0; c < 4; ++c)
            out[4 * i + c] = sat(static_cast<int>(static_cast<float>(in[4 * i + c]) * g) + out[4 * i + c]);
    }
}
// FUN_18004a380: 2D emitter panner (writes, does not accumulate)
void pan2d(uint32_t n, const int16_t* in, int16_t* out, float g0, float gs, float p0, float ps) {
    float p1023 = p0 * 1023.0f;
    for (uint32_t i = 0; i < n; ++i) {
        float g = static_cast<float>(i) * gs + g0;
        if (1.0f <= g) g = 1.0f;
        uint32_t idx = static_cast<uint32_t>(static_cast<int64_t>(static_cast<float>(i) * ps * 1023.0f + p1023)) & 0x3ff;
        out[2 * i] = sat(static_cast<int>(static_cast<float>(in[2 * i]) * g * kPan.t[idx]));
        out[2 * i + 1] = sat(static_cast<int>(static_cast<float>(in[2 * i + 1]) * g * kPan.t[0x3ff - idx]));
    }
}
}  // namespace

void Engine::renderBlock(int16_t* out) {
    std::memset(out, 0, sizeof(int16_t) * 2 * kBlock);
    if (!speakers_.empty()) std::fill(speakerBuf_.begin(), speakerBuf_.end(), 0.0f);
    for (auto& e : emitters_)
        if (e) {
            std::fill(e->buf.begin(), e->buf.end(), int16_t(0));
            e->used = false;
        }
    // players (AudPlayer::mixdown), in player order
    for (auto& pp : players_) {
        if (!pp || pp->state != 2 || !pp->clip) continue;
        Player& p = *pp;
        int16_t* dst = out;
        Emitter* em = p.emitter ? emitter(p.emitter) : nullptr;
        if (p.emitter && !em) continue;
        if (em) {
            dst = em->buf.data();
            em->used = true;
        }
        auto beginRamp = [&](bool& pending, uint32_t req, float reqV, float cur, float& target, float& step, uint32_t& remain) {
            if (!pending) return;
            remain = req;
            uint32_t r = req % kBlock;
            if (req == 0 || r != 0) remain = (kBlock - r) + req;
            target = reqV;
            if (1.0f < reqV) target = 1.0f;
            step = (target - cur) / static_cast<float>(remain);
            pending = false;
        };
        beginRamp(p.volPending, p.volReqFrames, p.volReq, p.vol, p.volTarget, p.volStep, p.volRemain);
        beginRamp(p.panPending, p.panReqFrames, p.panReq, p.pan, p.panTarget, p.panStep, p.panRemain);
        const PcmClip& c = *p.clip;
        uint32_t mixed = 0;
        while (mixed < kBlock) {
            if (p.pos >= c.frames) {
                if (p.loop && c.frames) {
                    p.pos = 0;
                } else {
                    p.state = 0;  // end: stopped, rewound
                    p.pos = 0;
                    break;
                }
            }
            uint32_t n = std::min(kBlock - mixed, c.frames - p.pos);
            const int16_t* src = c.data.data() + static_cast<size_t>(p.pos) * static_cast<size_t>(c.channels);
            int16_t* d = dst + static_cast<size_t>(mixed) * (em && em->type == 3 ? 4 : 2);
            if (c.channels == 1) mixMono(n, src, d, p.vol, p.volStep, p.pan, p.panStep);
            else if (c.channels == 2) mixStereo(n, src, d, p.vol, p.volStep, p.pan, p.panStep);
            else if (c.channels == 4 && em && em->type == 3) mixQuad(n, src, d, p.vol, p.volStep);
            p.pos += n;
            mixed += n;
        }
        if (p.volRemain != 0) {
            uint32_t rem = mixed <= p.volRemain ? p.volRemain - mixed : 0;
            p.volRemain = rem;
            p.vol = static_cast<float>(mixed) * p.volStep + p.vol;
            if (rem == 0) { p.vol = p.volTarget; p.volStep = 0; }
            if (1.0f < p.vol) p.vol = 1.0f;
        }
        if (p.panRemain != 0) {
            uint32_t rem = mixed <= p.panRemain ? p.panRemain - mixed : 0;
            p.panRemain = rem;
            p.pan = static_cast<float>(mixed) * p.panStep + p.pan;
            if (rem == 0) { p.pan = p.panTarget; p.panStep = 0; }
            if (1.0f < p.pan) p.pan = 1.0f;
        }
        if (p.panRemain != 0) {  // the engine advances the pan ramp a second time (as decompiled)
            uint32_t rem = mixed <= p.panRemain ? p.panRemain - mixed : 0;
            p.panRemain = rem;
            p.pan = static_cast<float>(mixed) * p.panStep + p.pan;
            if (rem == 0) { p.pan = p.panTarget; p.panStep = 0; }
        }
    }
    // emitters: effects, then 2D output (FUN_180040ad0)
    std::vector<int16_t> tmp(static_cast<size_t>(kBlock) * 2);
    for (auto& ep : emitters_) {
        if (!ep || !ep->used) continue;
        Emitter& e = *ep;
        if (e.type == 1) {
            update2d(e);
            float gs = (e.gainTarget - e.gainPrev) / static_cast<float>(kBlock);
            float ps = (e.panTarget - e.panPrev) / static_cast<float>(kBlock);
            pan2d(kBlock, e.buf.data(), tmp.data(), e.gainPrev, gs, e.panPrev, ps);
            e.panPrev = e.panTarget;
            e.gainPrev = e.gainTarget;
            for (uint32_t i = 0; i < 2 * kBlock; ++i) out[i] = sat(static_cast<int>(out[i]) + static_cast<int>(tmp[i]));
        } else if (e.type == 3) {
            for (uint32_t i = 0; i < kBlock; ++i) {
                const int16_t* s = e.buf.data() + 4 * i;
                float ch[4];
                for (int c = 0; c < 4; ++c) {
                    float x = static_cast<float>(s[c]) * kInt16ToFloat;
                    float h = e.hp[c].run(x);
                    float l = e.lp[c].run(x);
                    ch[c] = l - h * (c == 0 ? kShelfW : kShelfXYZ);
                }
                float* o = e.scratch.data() + 4 * i;
                o[0] = ch[0];
                o[1] = ch[2] * e.m[1] + ch[1] * e.m[0] + ch[3] * e.m[2];
                o[2] = ch[1] * e.m[3] + ch[2] * e.m[4] + ch[3] * e.m[5];
                o[3] = ch[1] * e.m[6] + ch[2] * e.m[7] + ch[3] * e.m[8];
            }
            if (!speakers_.empty())
                for (int k = 0; k < kSpeakers; ++k) {
                    float* sb = speakerBuf_.data() + static_cast<size_t>(k) * kBlock * 2;
                    const float* dc = kDecode[k];
                    for (uint32_t i = 0; i < kBlock; ++i) {
                        const float* o = e.scratch.data() + 4 * i;
                        sb[2 * i] = dc[1] * o[1] + dc[0] * o[0] + dc[3] * o[3] + sb[2 * i];
                        sb[2 * i + 1] = dc[2] * o[2] + sb[2 * i + 1];
                    }
                }
        }
    }
    // virtual speakers: binaural filter, L = M+S, R = M-S, subtracted * -32767 (FUN_18003f8c0)
    for (size_t k = 0; k < speakers_.size(); ++k) {
        float* sb = speakerBuf_.data() + k * kBlock * 2;
        for (uint32_t i = 0; i < kBlock; ++i) {
            float o[2];
            speakers_[k]->process(sb + 2 * i, o);
            float l = o[1] + o[0], r = o[0] - o[1];
            out[2 * i] = sat(static_cast<int>(out[2 * i]) - static_cast<int>(l * -32767.0f));
            out[2 * i + 1] = sat(static_cast<int>(out[2 * i + 1]) - static_cast<int>(r * -32767.0f));
        }
    }
    if (master_ != 1.0f)
        for (uint32_t i = 0; i < 2 * kBlock; ++i)
            out[i] = static_cast<int16_t>(static_cast<int>(static_cast<float>(out[i]) * master_));
}

void Engine::render(int16_t* out, uint32_t frames) {
    std::lock_guard<std::mutex> g(mutex_);
    uint32_t done = 0;
    while (done < frames) {
        if (pendingPos_ >= pending_.size()) {
            pending_.resize(static_cast<size_t>(kBlock) * 2);
            renderBlock(pending_.data());
            pendingPos_ = 0;
        }
        uint32_t avail = static_cast<uint32_t>((pending_.size() - pendingPos_) / 2);
        uint32_t n = std::min(avail, frames - done);
        std::memcpy(out + static_cast<size_t>(done) * 2, pending_.data() + pendingPos_, sizeof(int16_t) * 2 * n);
        pendingPos_ += static_cast<size_t>(n) * 2;
        done += n;
    }
}

}  // namespace oyster::audio
