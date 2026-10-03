// Package file system: resolves Moxie URIs ("package:path") against the user's installation.
// The original runs on case-insensitive NTFS and references some files with different case
// (e.g. "PEARLPACKAGE/TEXTURES/vrsplash.DDS"), so lookups go through a lower-case index.
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace oyster {

class PackageFS {
public:
    // root = installation directory containing common/, story/, pearl_vrcam/, pearlpackage/.
    explicit PackageFS(const std::string& root);

    // "pkg:path", "pkg/path" or a path relative to root -> absolute path ("" if missing).
    std::string resolve(const std::string& uri) const;
    bool exists(const std::string& uri) const { return !resolve(uri).empty(); }
    std::vector<uint8_t> read(const std::string& uri) const;  // throws if missing
    const std::string& root() const { return root_; }
    size_t fileCount() const { return index_.size(); }

    static std::string normalize(const std::string& uri);  // lower case, '/' separators

private:
    std::string root_;
    std::unordered_map<std::string, std::string> index_;  // normalized relative -> real relative
};

std::vector<uint8_t> readFile(const std::string& path);

}  // namespace oyster
