#include "minedict/mapped_file.hpp"

#include <cerrno>
#include <cstring>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace minedict {

MappedFile::MappedFile(MappedFile&& other) noexcept
    : path_(std::move(other.path_)),
      data_(std::exchange(other.data_, nullptr)),
      size_(std::exchange(other.size_, 0)),
      mapping_(std::exchange(other.mapping_, nullptr)) {}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
  if (this != &other) {
    release();
    path_ = std::move(other.path_);
    data_ = std::exchange(other.data_, nullptr);
    size_ = std::exchange(other.size_, 0);
    mapping_ = std::exchange(other.mapping_, nullptr);
  }
  return *this;
}

MappedFile::~MappedFile() { release(); }

#if defined(_WIN32)

Result<MappedFile> MappedFile::open(const std::filesystem::path& path) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec)) return fail(ErrorKind::io, "{}: no such file", path.string());
  const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return fail(ErrorKind::io, "{}: cannot open for reading", path.string());
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0) {
    CloseHandle(file);
    return fail(ErrorKind::format, "{}: the file is empty", path.string());
  }
  const HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
  CloseHandle(file);
  if (mapping == nullptr) return fail(ErrorKind::io, "{}: cannot map into memory", path.string());
  void* data = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
  if (data == nullptr) {
    CloseHandle(mapping);
    return fail(ErrorKind::io, "{}: cannot map into memory", path.string());
  }
  MappedFile mapped;
  mapped.path_ = path;
  mapped.data_ = data;
  mapped.size_ = static_cast<std::size_t>(size.QuadPart);
  mapped.mapping_ = mapping;
  return mapped;
}

void MappedFile::release() noexcept {
  if (data_ != nullptr) UnmapViewOfFile(data_);
  if (mapping_ != nullptr) CloseHandle(static_cast<HANDLE>(mapping_));
  data_ = nullptr;
  mapping_ = nullptr;
  size_ = 0;
}

#else

Result<MappedFile> MappedFile::open(const std::filesystem::path& path) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec)) return fail(ErrorKind::io, "{}: no such file", path.string());
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) return fail(ErrorKind::io, "{}: cannot open for reading ({})", path.string(), std::strerror(errno));
  struct stat info{};
  if (fstat(fd, &info) != 0) {
    ::close(fd);
    return fail(ErrorKind::io, "{}: cannot stat ({})", path.string(), std::strerror(errno));
  }
  if (info.st_size <= 0) {
    ::close(fd);
    return fail(ErrorKind::format, "{}: the file is empty", path.string());
  }
  const auto size = static_cast<std::size_t>(info.st_size);
  void* data = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
  ::close(fd);
  if (data == MAP_FAILED) return fail(ErrorKind::io, "{}: cannot map into memory ({})", path.string(), std::strerror(errno));
  MappedFile mapped;
  mapped.path_ = path;
  mapped.data_ = data;
  mapped.size_ = size;
  return mapped;
}

void MappedFile::release() noexcept {
  if (data_ != nullptr) munmap(data_, size_);
  data_ = nullptr;
  size_ = 0;
}

#endif

}  // namespace minedict
