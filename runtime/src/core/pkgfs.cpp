#include "core/pkgfs.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace oyster {

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    f.seekg(0, std::ios::end);
    std::vector<uint8_t> v(static_cast<size_t>(f.tellg()));
    f.seekg(0);
    if (!v.empty()) f.read(reinterpret_cast<char*>(v.data()), static_cast<std::streamsize>(v.size()));
    return v;
}

std::string PackageFS::normalize(const std::string& uri) {
    std::string s = uri;
    auto colon = s.find(':');
    // "pkg:path" -> "pkg/path" (but keep drive letters out: URIs never contain them)
    if (colon != std::string::npos && colon > 1) s[colon] = '/';
    for (auto& c : s) {
        if (c == '\\') c = '/';
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    while (!s.empty() && s[0] == '/') s.erase(0, 1);
    return s;
}

PackageFS::PackageFS(const std::string& root) : root_(root) {
    fs::path r = fs::u8path(root);
    if (!fs::is_directory(r)) throw std::runtime_error("not a directory: " + root);
    for (auto it = fs::recursive_directory_iterator(r); it != fs::recursive_directory_iterator(); ++it) {
        if (!it->is_regular_file()) continue;
        std::string rel = fs::relative(it->path(), r).generic_u8string();
        index_[normalize(rel)] = rel;
    }
}

std::string PackageFS::resolve(const std::string& uri) const {
    auto it = index_.find(normalize(uri));
    if (it == index_.end()) return {};
    return (fs::u8path(root_) / fs::u8path(it->second)).u8string();
}

std::vector<std::string> PackageFS::list(const std::string& suffix) const {
    std::string sfx = normalize(suffix);
    std::vector<std::string> out;
    for (const auto& kv : index_)
        if (kv.first.size() >= sfx.size() && kv.first.compare(kv.first.size() - sfx.size(), sfx.size(), sfx) == 0)
            out.push_back(kv.first);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<uint8_t> PackageFS::read(const std::string& uri) const {
    std::string p = resolve(uri);
    if (p.empty()) throw std::runtime_error("missing asset " + uri);
    return readFile(p);
}

}  // namespace oyster
