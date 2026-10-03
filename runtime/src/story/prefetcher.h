// Background loading for the story's ResourceGroups (System.createResourceGroup / addResource /
// prefetch, see common/scripts/resources.lua and story/scripts/commands.lua): when the story
// enters a state it prefetches every possible next state - models, animations, audio. Worker
// threads load and decode them (and read the texture files a model's materials reference) while
// the current state plays; the main thread takes the results when the story asks for them and
// only waits if a load is still running ("coerced" in the original's status).
#pragma once
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "assets/model.h"
#include "assets/anim.h"
#include "audio/audio_engine.h"
#include "core/pkgfs.h"

namespace oyster::story {

class Prefetcher {
public:
    enum class Kind { Model, Anim, Audio, File };
    Prefetcher(const PackageFS& fs, unsigned threads);
    ~Prefetcher();

    void request(Kind kind, const std::string& uri);
    void drop(Kind kind, const std::string& uri);  // result not needed any more (unused branch)

    // Results move to the caller. `found` is false when the resource was never requested; when
    // it is still loading the call waits for it.
    std::shared_ptr<ModelResource> takeModel(const std::string& uri, bool& found);
    std::shared_ptr<AnimResource> takeAnim(const std::string& uri, bool& found);
    std::shared_ptr<const audio::PcmClip> takeAudio(const std::string& uri, bool& found);
    bool takeFile(const std::string& uri, std::vector<uint8_t>& bytes);

    size_t pending() const;
    // Texture files read ahead and not taken yet (URIs as requested), at most `max`.
    void readyFiles(std::vector<std::string>& out, size_t max) const;
    void expire(double maxAgeSeconds);  // forget finished results nobody took
    double waitedMs = 0;  // main-thread time spent waiting for unfinished loads

private:
    struct Entry {
        Kind kind;
        std::string uri;
        std::string raw;  // as first requested (material URIs, e.g. "pearl:textures/x.dds")
        bool done = false, failed = false;
        std::chrono::steady_clock::time_point doneAt;
        std::shared_ptr<ModelResource> model;
        std::shared_ptr<AnimResource> anim;
        std::shared_ptr<const audio::PcmClip> clip;
        std::vector<uint8_t> bytes;
    };
    using Key = std::pair<int, std::string>;
    std::shared_ptr<Entry> takeEntry(Kind kind, const std::string& uri);
    void work();
    void load(Entry& e);

    const PackageFS& fs_;
    mutable std::mutex m_;
    std::condition_variable cv_, doneCv_;
    std::deque<std::shared_ptr<Entry>> queue_;
    std::map<Key, std::shared_ptr<Entry>> entries_;
    std::vector<std::thread> threads_;
    bool stop_ = false;
};

}  // namespace oyster::story
