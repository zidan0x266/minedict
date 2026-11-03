// minedict - a read-only memory-mapped file.
#pragma once

#include <cstddef>
#include <filesystem>

#include "minedict/bytes.hpp"

namespace minedict {

class MappedFile {
 public:
  [[nodiscard]] static Result<MappedFile> open(const std::filesystem::path& path);

  MappedFile() = default;
  MappedFile(MappedFile&& other) noexcept;
  MappedFile& operator=(MappedFile&& other) noexcept;
  MappedFile(const MappedFile&) = delete;
  MappedFile& operator=(const MappedFile&) = delete;
  ~MappedFile();

  [[nodiscard]] ByteView view() const noexcept { return {static_cast<const std::byte*>(data_), size_}; }
  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

 private:
  void release() noexcept;

  std::filesystem::path path_;
  void* data_{nullptr};
  std::size_t size_{0};
  void* mapping_{nullptr};  // Windows file-mapping handle; unused elsewhere
};

}  // namespace minedict
