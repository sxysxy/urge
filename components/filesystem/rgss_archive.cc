// Copyright 2026 HfCloud(sxysxygm@gmail.com).
// Use of this source code is governed by a BSD - style license that can be
// found in the LICENSE file.

#include "components/filesystem/rgss_archive.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace filesystem {

namespace {

constexpr uint32_t kV1InitKey = 0xDEADCAFE;

bool ReadU32LE(std::ifstream& file, uint32_t* value) {
  uint8_t bytes[4];
  file.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
  if (!file.good())
    return false;

  *value = static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
  return true;
}

std::string RemoveExtName(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  const size_t dot = path.find_last_of('.');
  if (dot == std::string::npos)
    return path;
  if (slash != std::string::npos && dot < slash)
    return path;
  return path.substr(0, dot);
}

const char* FindExtName(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  const size_t dot = path.find_last_of('.');
  if (dot == std::string::npos)
    return nullptr;
  if (slash != std::string::npos && dot < slash)
    return nullptr;
  return path.c_str() + dot + 1;
}

}  // namespace

bool RgssArchive::IsSupportedArchivePath(const std::string& path) {
  std::string normalized = NormalizePath(path);
  return normalized.size() >= 7 &&
         (normalized.ends_with(".rgssad") || normalized.ends_with(".rgss2a") ||
          normalized.ends_with(".rgss3a"));
}

std::string RgssArchive::NormalizePath(const std::string& path) {
  std::string result = path;
  std::replace(result.begin(), result.end(), '\\', '/');
  return ToLower(result);
}

bool RgssArchive::Open(const std::string& archive_path,
                       const std::string& mount_point,
                       std::string* error_message) {
  std::ifstream file(archive_path, std::ios::binary);
  if (!file.is_open()) {
    if (error_message)
      *error_message = "Failed to open archive: " + archive_path;
    return false;
  }

  file.seekg(0, std::ios::end);
  const uint64_t file_size = file.tellg();
  file.seekg(0, std::ios::beg);

  if (file_size < 8) {
    if (error_message)
      *error_message = "Archive is too small: " + archive_path;
    return false;
  }

  char magic[7];
  file.read(magic, sizeof(magic));
  if (file.gcount() != sizeof(magic) || std::string(magic, sizeof(magic)) != "RGSSAD") {
    if (error_message)
      *error_message = "Invalid archive header: " + archive_path;
    return false;
  }

  uint8_t version = 0;
  file.read(reinterpret_cast<char*>(&version), 1);
  if (!file.good()) {
    if (error_message)
      *error_message = "Failed to read archive version: " + archive_path;
    return false;
  }

  archive_path_ = archive_path;
  mount_point_ = NormalizePath(mount_point);
  entries_.clear();
  lookup_by_full_name_.clear();
  lookup_by_name_noext_.clear();

  if (version == static_cast<uint8_t>(Version::kV1)) {
    version_ = Version::kV1;
    return ReadV1(file, file_size, error_message);
  }
  if (version == static_cast<uint8_t>(Version::kV3)) {
    version_ = Version::kV3;
    return ReadV3(file, file_size, error_message);
  }

  if (error_message)
    *error_message = "Unsupported RGSS archive version: " + std::to_string(version);
  return false;
}

bool RgssArchive::Contains(const std::string& path) const {
  const std::string normalized = NormalizePath(path);
  if (lookup_by_full_name_.contains(normalized))
    return true;

  const std::string normalized_noext = RemoveExtName(normalized);
  return lookup_by_name_noext_.contains(normalized_noext);
}

bool RgssArchive::ExtractByPath(const std::string& path,
                                std::vector<uint8_t>* output,
                                std::string* ext_name,
                                std::string* error_message) const {
  if (!output) {
    if (error_message)
      *error_message = "Invalid output pointer.";
    return false;
  }

  const std::string normalized = NormalizePath(path);
  auto it = lookup_by_full_name_.find(normalized);
  if (it != lookup_by_full_name_.end()) {
    if (ext_name) {
      const char* ext = FindExtName(entries_[it->second].normalized_name);
      *ext_name = ext ? ext : "";
    }
    return DecryptFileData(entries_[it->second], output, error_message);
  }

  const std::string normalized_noext = RemoveExtName(normalized);
  auto stem_it = lookup_by_name_noext_.find(normalized_noext);
  if (stem_it == lookup_by_name_noext_.end() || stem_it->second.empty()) {
    if (error_message)
      *error_message = "No file match in archive: " + path;
    return false;
  }

  const FileEntry& entry = entries_[stem_it->second.front()];
  if (ext_name) {
    const char* ext = FindExtName(entry.normalized_name);
    *ext_name = ext ? ext : "";
  }
  return DecryptFileData(entry, output, error_message);
}

bool RgssArchive::EnumerateDir(const std::string& dir,
                               std::vector<std::string>* files) const {
  if (!files)
    return false;

  const std::string normalized_dir = NormalizePath(dir);
  std::string prefix = normalized_dir;
  if (!prefix.empty() && !prefix.ends_with('/'))
    prefix.push_back('/');

  for (const auto& entry : entries_) {
    if (!prefix.empty()) {
      if (!entry.normalized_name.starts_with(prefix))
        continue;
      const std::string rest = entry.normalized_name.substr(prefix.size());
      if (rest.find('/') != std::string::npos)
        continue;
      files->push_back(rest);
      continue;
    }

    if (entry.normalized_name.find('/') == std::string::npos)
      files->push_back(entry.normalized_name);
  }

  return true;
}

std::string RgssArchive::BuildLookupName(const std::string& mount_point,
                                         const std::string& name) {
  std::string result = NormalizePath(name);
  if (mount_point.empty())
    return result;
  if (result.empty())
    return mount_point;
  return mount_point + "/" + result;
}

uint32_t RgssArchive::CalcNextKey(uint32_t key) {
  key *= 7;
  key += 3;
  return key;
}

std::string RgssArchive::ToLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool RgssArchive::ReadV1(std::ifstream& file,
                         uint64_t file_size,
                         std::string* error_message) {
  file.seekg(8, std::ios::beg);
  uint32_t key = kV1InitKey;

  while (true) {
    if (static_cast<uint64_t>(file.tellg()) >= file_size)
      break;

    uint32_t raw_length = 0;
    if (!ReadU32LE(file, &raw_length)) {
      if (error_message)
        *error_message = "Corrupted RGSSAD v1 length field.";
      return false;
    }

    const uint32_t name_length = raw_length ^ key;
    key = CalcNextKey(key);
    if (name_length == 0 || name_length > file_size) {
      if (error_message)
        *error_message = "Invalid RGSSAD v1 filename length.";
      return false;
    }

    std::string encrypted_name(name_length, '\0');
    file.read(encrypted_name.data(), encrypted_name.size());
    if (!file.good()) {
      if (error_message)
        *error_message = "Corrupted RGSSAD v1 filename data.";
      return false;
    }

    std::string name(name_length, '\0');
    for (uint32_t i = 0; i < name_length; ++i) {
      name[i] = static_cast<char>(
          static_cast<uint8_t>(encrypted_name[i]) ^ static_cast<uint8_t>(key & 0xFF));
      key = CalcNextKey(key);
    }

    uint32_t raw_size = 0;
    if (!ReadU32LE(file, &raw_size)) {
      if (error_message)
        *error_message = "Corrupted RGSSAD v1 size field.";
      return false;
    }

    const uint32_t file_size_decoded = raw_size ^ key;
    key = CalcNextKey(key);

    const uint64_t offset = static_cast<uint64_t>(file.tellg());
    if (offset + file_size_decoded > file_size) {
      if (error_message)
        *error_message = "RGSSAD v1 entry exceeds archive size.";
      return false;
    }

    FileEntry entry;
    entry.name = name;
    entry.normalized_name = BuildLookupName(mount_point_, name);
    entry.offset = static_cast<uint32_t>(offset);
    entry.size = file_size_decoded;
    entry.key = key;

    const size_t index = entries_.size();
    entries_.push_back(entry);
    lookup_by_full_name_[entry.normalized_name] = index;
    lookup_by_name_noext_[RemoveExtName(entry.normalized_name)].push_back(index);

    file.seekg(file_size_decoded, std::ios::cur);
    if (!file.good()) {
      if (error_message)
        *error_message = "Corrupted RGSSAD v1 payload.";
      return false;
    }
  }

  return true;
}

bool RgssArchive::ReadV3(std::ifstream& file,
                         uint64_t file_size,
                         std::string* error_message) {
  file.seekg(8, std::ios::beg);

  uint32_t raw_key = 0;
  if (!ReadU32LE(file, &raw_key)) {
    if (error_message)
      *error_message = "Corrupted RGSSAD v3 key header.";
    return false;
  }

  uint32_t key = raw_key;
  key *= 9;
  key += 3;

  while (true) {
    uint32_t raw_offset = 0;
    uint32_t raw_size = 0;
    uint32_t raw_file_key = 0;
    uint32_t raw_length = 0;
    if (!ReadU32LE(file, &raw_offset) || !ReadU32LE(file, &raw_size) ||
        !ReadU32LE(file, &raw_file_key) || !ReadU32LE(file, &raw_length)) {
      if (error_message)
        *error_message = "Corrupted RGSSAD v3 table fields.";
      return false;
    }

    const uint32_t offset = raw_offset ^ key;
    const uint32_t size = raw_size ^ key;
    const uint32_t file_key = raw_file_key ^ key;
    const uint32_t name_length = raw_length ^ key;

    if (offset == 0)
      break;

    if (name_length == 0 || name_length > file_size) {
      if (error_message)
        *error_message = "Invalid RGSSAD v3 filename length.";
      return false;
    }

    std::string encrypted_name(name_length, '\0');
    file.read(encrypted_name.data(), encrypted_name.size());
    if (!file.good()) {
      if (error_message)
        *error_message = "Corrupted RGSSAD v3 filename data.";
      return false;
    }

    uint8_t key_bytes[4] = {
        static_cast<uint8_t>((key >> 0) & 0xFF),
        static_cast<uint8_t>((key >> 8) & 0xFF),
        static_cast<uint8_t>((key >> 16) & 0xFF),
        static_cast<uint8_t>((key >> 24) & 0xFF),
    };

    std::string name(name_length, '\0');
    for (uint32_t i = 0; i < name_length; ++i)
      name[i] = static_cast<char>(static_cast<uint8_t>(encrypted_name[i]) ^ key_bytes[i % 4]);

    if (static_cast<uint64_t>(offset) + size > file_size) {
      if (error_message)
        *error_message = "RGSSAD v3 entry exceeds archive size.";
      return false;
    }

    FileEntry entry;
    entry.name = name;
    entry.normalized_name = BuildLookupName(mount_point_, name);
    entry.offset = offset;
    entry.size = size;
    entry.key = file_key;

    const size_t index = entries_.size();
    entries_.push_back(entry);
    lookup_by_full_name_[entry.normalized_name] = index;
    lookup_by_name_noext_[RemoveExtName(entry.normalized_name)].push_back(index);
  }

  return true;
}

bool RgssArchive::DecryptFileData(const FileEntry& entry,
                                  std::vector<uint8_t>* output,
                                  std::string* error_message) const {
  std::ifstream file(archive_path_, std::ios::binary);
  if (!file.is_open()) {
    if (error_message)
      *error_message = "Failed to open archive: " + archive_path_;
    return false;
  }

  file.seekg(entry.offset, std::ios::beg);
  if (!file.good()) {
    if (error_message)
      *error_message = "Invalid archive offset for entry: " + entry.name;
    return false;
  }

  std::vector<uint8_t> encrypted(entry.size);
  file.read(reinterpret_cast<char*>(encrypted.data()), encrypted.size());
  if (static_cast<uint32_t>(file.gcount()) != entry.size) {
    if (error_message)
      *error_message = "Failed to read encrypted data: " + entry.name;
    return false;
  }

  output->resize(entry.size);

  uint32_t temp_key = entry.key;
  uint8_t key_bytes[4] = {
      static_cast<uint8_t>((temp_key >> 0) & 0xFF),
      static_cast<uint8_t>((temp_key >> 8) & 0xFF),
      static_cast<uint8_t>((temp_key >> 16) & 0xFF),
      static_cast<uint8_t>((temp_key >> 24) & 0xFF),
  };

  int j = 0;
  for (uint32_t i = 0; i < entry.size; ++i) {
    if (j == 4) {
      j = 0;
      temp_key = CalcNextKey(temp_key);
      key_bytes[0] = static_cast<uint8_t>((temp_key >> 0) & 0xFF);
      key_bytes[1] = static_cast<uint8_t>((temp_key >> 8) & 0xFF);
      key_bytes[2] = static_cast<uint8_t>((temp_key >> 16) & 0xFF);
      key_bytes[3] = static_cast<uint8_t>((temp_key >> 24) & 0xFF);
    }

    (*output)[i] = encrypted[i] ^ key_bytes[j];
    ++j;
  }

  return true;
}

}  // namespace filesystem
