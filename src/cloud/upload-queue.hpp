#pragma once

#include "cloud/upload-job.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace ReplayBufferPro::Cloud {

class UploadQueue {
public:
  explicit UploadQueue(std::filesystem::path persistencePath);
  bool load(std::string *error = nullptr);
  bool save(std::string *error = nullptr) const;
  void add(UploadJob job);
  std::optional<std::size_t> nextReady(std::int64_t nowEpochSeconds) const;
  std::vector<UploadJob> &jobs() { return items; }
  const std::vector<UploadJob> &jobs() const { return items; }
  std::size_t pendingCount() const;

private:
  std::filesystem::path persistencePath;
  std::vector<UploadJob> items;
};

} // namespace ReplayBufferPro::Cloud
