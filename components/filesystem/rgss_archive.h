// Copyright 2026 HfCloud(sxysxygm@gmail.com).
// Use of this source code is governed by a BSD - style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_FILESYSTEM_RGSS_ARCHIVE_H_
#define COMPONENTS_FILESYSTEM_RGSS_ARCHIVE_H_

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace filesystem {

class RgssArchive {
 public:
  struct FileEntry {
    std::string name;
    std::string normalized_name;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t key = 0;
  };

  enum class Version {
    kUnknown = 0,
    kV1 = 1,
    kV3 = 3,
  };

  RgssArchive() = default;
  ~RgssArchive() = default;

  RgssArchive(const RgssArchive&) = delete;
  RgssArchive& operator=(const RgssArchive&) = delete;

  static bool IsSupportedArchivePath(const std::string& path);
  static std::string NormalizePath(const std::string& path);

  bool Open(const std::string& archive_path,
            const std::string& mount_point,
            std::string* error_message);

  bool Contains(const std::string& path) const;
  bool ExtractByPath(const std::string& path,
                     std::vector<uint8_t>* output,
                     std::string* ext_name,
                     std::string* error_message) const;
  bool EnumerateDir(const std::string& dir, std::vector<std::string>* files) const;

  const std::string& archive_path() const { return archive_path_; }
  Version version() const { return version_; }

 private:
  static std::string BuildLookupName(const std::string& mount_point,
                                     const std::string& name);
  static uint32_t CalcNextKey(uint32_t key);
  static std::string ToLower(std::string value);

  bool ReadV1(std::ifstream& file, uint64_t file_size, std::string* error_message);
  bool ReadV3(std::ifstream& file, uint64_t file_size, std::string* error_message);
  bool DecryptFileData(const FileEntry& entry,
                       std::vector<uint8_t>* output,
                       std::string* error_message) const;

  std::string archive_path_;
  std::string mount_point_;
  Version version_ = Version::kUnknown;
  std::vector<FileEntry> entries_;
  std::unordered_map<std::string, size_t> lookup_by_full_name_;
  std::unordered_map<std::string, std::vector<size_t>> lookup_by_name_noext_;
};

}  // namespace filesystem

#endif  // COMPONENTS_FILESYSTEM_RGSS_ARCHIVE_H_
