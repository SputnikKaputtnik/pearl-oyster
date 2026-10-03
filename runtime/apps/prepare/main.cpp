// oyster_prepare: offline installer step for devices without S3TC (Meta Quest).
//
// Copies the content folders of the user's Pearl installation (read-only source) into an
// output folder and converts every DXT5 texture to an uncompressed RGBA8 DDS of the same name,
// decoded with the same arithmetic the runtime fallback uses (assets/dds.cpp, NVIDIA BC3
// interpolation - frame-identical to the desktop GPU the original ran on). All other files are
// copied unchanged, so story scripts and URIs are untouched. The device then uploads textures
// directly instead of decoding them while the story plays.
//
// Usage: oyster_prepare --root <install> --out <folder> [--threads N]
//   result: <folder>/{common,pearl_vrcam,pearlpackage,story} + oyster-prepared.txt
//   then:   adb push <folder>/. /sdcard/Android/data/org.oyster.pearl/files/pearl/
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "assets/dds.h"
#include "core/pkgfs.h"

namespace fs = std::filesystem;
using namespace oyster;

namespace {

const char* kFolders[] = {"common", "pearl_vrcam", "pearlpackage", "story"};

void writeFile(const fs::path& p, const uint8_t* data, size_t size) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("cannot write " + p.u8string());
    f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!f) throw std::runtime_error("write failed " + p.u8string());
}

// uncompressed 32-bit DDS, bytes R,G,B,A (the layout assets/dds.cpp reads as TexFormat::RGBA8)
std::vector<uint8_t> rgbaDDS(uint32_t w, uint32_t h, const std::vector<uint8_t>& rgba) {
    std::vector<uint8_t> out(128 + rgba.size(), 0);
    auto u32 = [&](size_t o, uint32_t v) { std::memcpy(&out[o], &v, 4); };
    std::memcpy(out.data(), "DDS ", 4);
    u32(4, 124);
    u32(8, 0x0000100F);  // CAPS | HEIGHT | WIDTH | PITCH | PIXELFORMAT
    u32(12, h);
    u32(16, w);
    u32(20, w * 4);
    u32(76, 32);         // pixel format size
    u32(80, 0x41);       // DDPF_RGB | DDPF_ALPHAPIXELS
    u32(88, 32);
    u32(92, 0x000000FF);
    u32(96, 0x0000FF00);
    u32(100, 0x00FF0000);
    u32(104, 0xFF000000);
    u32(108, 0x1000);    // DDSCAPS_TEXTURE
    std::memcpy(out.data() + 128, rgba.data(), rgba.size());
    return out;
}

bool isDXT5(const std::vector<uint8_t>& d) {
    return d.size() >= 128 && !std::memcmp(d.data(), "DDS ", 4) && !std::memcmp(d.data() + 84, "DXT5", 4);
}

}  // namespace

int main(int argc, char** argv) {
    std::string root, out;
    unsigned threads = std::max(1u, std::thread::hardware_concurrency());
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (k == "--root") root = next();
        else if (k == "--out") out = next();
        else if (k == "--threads") threads = static_cast<unsigned>(std::max(1, std::stoi(next())));
        else { std::fprintf(stderr, "unknown argument %s\n", k.c_str()); return 2; }
    }
    if (root.empty() || out.empty()) {
        std::fprintf(stderr, "usage: oyster_prepare --root <Pearl installation> --out <folder> [--threads N]\n");
        return 2;
    }
    fs::path src = fs::u8path(root), dst = fs::u8path(out);
    std::error_code ec;
    fs::path srcAbs = fs::weakly_canonical(src, ec), dstAbs = fs::weakly_canonical(dst, ec);
    // never write into the installation
    auto rel = fs::relative(dstAbs, srcAbs, ec);
    if (dstAbs == srcAbs || (!ec && !rel.empty() && *rel.begin() != "..")) {
        std::fprintf(stderr, "refusing to write into the installation (%s)\n", srcAbs.u8string().c_str());
        return 2;
    }
    std::vector<std::pair<fs::path, fs::path>> jobs;  // (source, relative)
    for (const char* folder : kFolders) {
        fs::path dir = src / folder;
        if (!fs::is_directory(dir)) {
            std::fprintf(stderr, "missing folder %s - is this a Pearl installation?\n", dir.u8string().c_str());
            return 1;
        }
        for (auto it = fs::recursive_directory_iterator(dir); it != fs::recursive_directory_iterator(); ++it)
            if (it->is_regular_file()) jobs.emplace_back(it->path(), fs::relative(it->path(), src));
    }
    std::atomic<size_t> next{0}, converted{0}, copied{0}, failed{0};
    std::atomic<uint64_t> bytesIn{0}, bytesOut{0};
    std::mutex logMutex;
    auto worker = [&]() {
        for (size_t j; (j = next++) < jobs.size();) {
            const auto& [from, relPath] = jobs[j];
            try {
                std::vector<uint8_t> data = readFile(from.u8string());
                bytesIn += data.size();
                fs::path to = dst / relPath;
                if (isDXT5(data)) {
                    TextureData t = loadDDS(data);
                    std::vector<uint8_t> rgba(static_cast<size_t>(t.width) * t.height * 4);
                    decodeDXT5(t.level0.data(), t.width, t.height, rgba.data());
                    std::vector<uint8_t> dds = rgbaDDS(t.width, t.height, rgba);
                    writeFile(to, dds.data(), dds.size());
                    bytesOut += dds.size();
                    ++converted;
                } else {
                    writeFile(to, data.data(), data.size());
                    bytesOut += data.size();
                    ++copied;
                }
            } catch (const std::exception& e) {
                std::lock_guard<std::mutex> lock(logMutex);
                std::fprintf(stderr, "%s: %s\n", relPath.u8string().c_str(), e.what());
                ++failed;
            }
            size_t done = converted + copied + failed;
            if (done % 500 == 0) {
                std::lock_guard<std::mutex> lock(logMutex);
                std::printf("%zu / %zu files\n", done, jobs.size());
                std::fflush(stdout);
            }
        }
    };
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; ++t) pool.emplace_back(worker);
    for (auto& t : pool) t.join();

    char manifest[1024];
    std::snprintf(manifest, sizeof(manifest),
                  "Oyster prepared package (oyster_prepare)\n"
                  "source: %s\n"
                  "files: %zu (%zu DXT5 textures converted to RGBA8 with the NVIDIA BC3 arithmetic, %zu copied unchanged)\n"
                  "bytes: %llu in, %llu out\n",
                  srcAbs.u8string().c_str(), jobs.size(), converted.load(), copied.load(),
                  static_cast<unsigned long long>(bytesIn.load()), static_cast<unsigned long long>(bytesOut.load()));
    writeFile(dst / "oyster-prepared.txt", reinterpret_cast<const uint8_t*>(manifest), std::strlen(manifest));
    std::printf("%s", manifest);
    if (failed) std::printf("FAILED: %zu files\n", failed.load());
    return failed ? 1 : 0;
}
