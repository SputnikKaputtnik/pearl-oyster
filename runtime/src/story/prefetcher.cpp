#include "story/prefetcher.h"

#include <chrono>
#include <cstdio>

namespace oyster::story {

Prefetcher::Prefetcher(const PackageFS& fs, unsigned threads) : fs_(fs) {
    for (unsigned i = 0; i < threads; ++i) threads_.emplace_back([this] { work(); });
}

Prefetcher::~Prefetcher() {
    {
        std::lock_guard<std::mutex> lock(m_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& t : threads_) t.join();
}

void Prefetcher::request(Kind kind, const std::string& uriIn) {
    std::string uri = PackageFS::normalize(uriIn);
    if (uri.empty()) return;
    {
        std::lock_guard<std::mutex> lock(m_);
        Key key{static_cast<int>(kind), uri};
        if (entries_.count(key)) return;
        auto e = std::make_shared<Entry>();
        e->kind = kind;
        e->uri = uri;
        e->raw = uriIn;
        entries_[key] = e;
        queue_.push_back(e);
    }
    cv_.notify_one();
}

void Prefetcher::expire(double maxAgeSeconds) {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(m_);
    for (auto it = entries_.begin(); it != entries_.end();) {
        const Entry& e = *it->second;
        if (e.done && std::chrono::duration<double>(now - e.doneAt).count() > maxAgeSeconds) it = entries_.erase(it);
        else ++it;
    }
}

void Prefetcher::drop(Kind kind, const std::string& uriIn) {
    std::lock_guard<std::mutex> lock(m_);
    entries_.erase({static_cast<int>(kind), PackageFS::normalize(uriIn)});
}

size_t Prefetcher::pending() const {
    std::lock_guard<std::mutex> lock(m_);
    size_t n = 0;
    for (const auto& kv : entries_) n += kv.second->done ? 0 : 1;
    return n;
}

void Prefetcher::readyFiles(std::vector<std::string>& out, size_t max) const {
    std::lock_guard<std::mutex> lock(m_);
    for (const auto& kv : entries_) {
        if (out.size() >= max) break;
        const Entry& e = *kv.second;
        if (e.kind == Kind::File && e.done && !e.failed) out.push_back(e.raw);
    }
}

void Prefetcher::readyModels(std::vector<std::shared_ptr<const ModelResource>>& out) const {
    std::lock_guard<std::mutex> lock(m_);
    for (const auto& kv : entries_) {
        const Entry& e = *kv.second;
        if (e.kind == Kind::Model && e.done && !e.failed && e.model) out.push_back(e.model);
    }
}

std::shared_ptr<Prefetcher::Entry> Prefetcher::takeEntry(Kind kind, const std::string& uriIn) {
    Key key{static_cast<int>(kind), PackageFS::normalize(uriIn)};
    std::unique_lock<std::mutex> lock(m_);
    lastTakeWaited = false;
    auto it = entries_.find(key);
    if (it == entries_.end()) return nullptr;
    std::shared_ptr<Entry> e = it->second;
    if (!e->done) {
        auto t0 = std::chrono::steady_clock::now();
        doneCv_.wait(lock, [&] { return e->done; });
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        waitedMs += ms;
        lastTakeWaited = ms > 2.0;
    }
    entries_.erase(key);
    return e;
}

std::shared_ptr<ModelResource> Prefetcher::takeModel(const std::string& uri, bool& found) {
    auto e = takeEntry(Kind::Model, uri);
    found = e && !e->failed;
    return e ? e->model : nullptr;
}

std::shared_ptr<AnimResource> Prefetcher::takeAnim(const std::string& uri, bool& found) {
    auto e = takeEntry(Kind::Anim, uri);
    found = e && !e->failed;
    return e ? e->anim : nullptr;
}

std::shared_ptr<const audio::PcmClip> Prefetcher::takeAudio(const std::string& uri, bool& found) {
    auto e = takeEntry(Kind::Audio, uri);
    found = e && !e->failed;
    return e ? e->clip : nullptr;
}

bool Prefetcher::takeFile(const std::string& uri, std::vector<uint8_t>& bytes) {
    auto e = takeEntry(Kind::File, uri);
    if (!e || e->failed) return false;
    bytes.swap(e->bytes);
    return true;
}

void Prefetcher::work() {
    for (;;) {
        std::shared_ptr<Entry> e;
        {
            std::unique_lock<std::mutex> lock(m_);
            cv_.wait(lock, [&] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            e = queue_.front();
            queue_.pop_front();
        }
        load(*e);
        {
            std::lock_guard<std::mutex> lock(m_);
            e->done = true;
            e->doneAt = std::chrono::steady_clock::now();
        }
        doneCv_.notify_all();
    }
}

void Prefetcher::load(Entry& e) {
    try {
        switch (e.kind) {
            case Kind::Model: {
                e.model = loadModel(fs_.read(e.uri));
                // the textures its materials sample, and the base model of a patch model
                if (e.model->isPatch()) request(Kind::Model, e.model->baseModel);
                for (const Material& m : e.model->materials)
                    for (const MaterialPass& p : m.passes)
                        for (const MaterialParam& prm : p.params)
                            if (prm.type != 0 && !prm.texture.uri.empty()) request(Kind::File, prm.texture.uri);
                break;
            }
            case Kind::Anim: e.anim = loadAnim(fs_.read(e.uri)); break;
            case Kind::Audio: e.clip = audio::decodeVorbis(fs_.read(e.uri)); break;
            case Kind::File: e.bytes = fs_.read(e.uri); break;
        }
    } catch (const std::exception& ex) {
        e.failed = true;
    }
}

}  // namespace oyster::story
