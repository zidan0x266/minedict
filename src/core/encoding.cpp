#include "minedict/encoding.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <utility>

#include "minedict/unicode.hpp"

#if MINEDICT_HAVE_ICONV
#include <iconv.h>
#endif

namespace minedict {
namespace {

std::string upper_ascii(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
  }
  return out;
}

std::string strip_separators(std::string_view text) {
  std::string out;
  for (const char c : text) {
    if (c != '-' && c != '_' && c != ' ') out += c;
  }
  return out;
}

#if MINEDICT_HAVE_ICONV
/// iconv charset names to try, most specific first.
std::array<const char*, 3> iconv_names(const Encoding& encoding) {
  switch (encoding.charset) {
    case Charset::gb18030: return {"GB18030", "GBK", "CP936"};
    case Charset::big5: return {"CP950", "BIG5-HKSCS", "BIG5"};
    default: return {encoding.name.c_str(), nullptr, nullptr};
  }
}
#endif

}  // namespace

Encoding Encoding::from_name(std::string_view declared) {
  const std::string key = strip_separators(upper_ascii(declared));
  if (key.empty() || key == "UTF8") return utf8();
  if (key == "UTF16" || key == "UTF16LE" || key == "UNICODE") return utf16le();
  if (key == "GBK" || key == "GB2312" || key == "GB18030" || key == "CP936" || key == "EUCCN") {
    return {Charset::gb18030, "GB18030"};
  }
  if (key == "BIG5" || key == "CP950" || key == "BIG5HKSCS") return {Charset::big5, "Big5"};
  if (key == "ISO88591" || key == "LATIN1" || key == "CP1252" || key == "WINDOWS1252") {
    return {Charset::latin1, "ISO-8859-1"};
  }
  return {Charset::other, std::string(declared)};
}

bool legacy_charsets_supported() noexcept {
#if MINEDICT_HAVE_ICONV
  return true;
#else
  return false;
#endif
}

Transcoder::Transcoder(Encoding encoding, void* handle) noexcept : encoding_(std::move(encoding)), handle_(handle) {}

Transcoder::Transcoder(Transcoder&& other) noexcept
    : encoding_(std::move(other.encoding_)), handle_(std::exchange(other.handle_, nullptr)) {}

Transcoder& Transcoder::operator=(Transcoder&& other) noexcept {
  if (this != &other) {
    this->~Transcoder();
    encoding_ = std::move(other.encoding_);
    handle_ = std::exchange(other.handle_, nullptr);
  }
  return *this;
}

Transcoder::~Transcoder() {
#if MINEDICT_HAVE_ICONV
  if (handle_ != nullptr) iconv_close(static_cast<iconv_t>(handle_));
#endif
  handle_ = nullptr;
}

Result<Transcoder> Transcoder::open(const Encoding& encoding) {
  if (encoding.is_unicode() || encoding.charset == Charset::latin1) return Transcoder{encoding, nullptr};
#if MINEDICT_HAVE_ICONV
  for (const char* name : iconv_names(encoding)) {
    if (name == nullptr) break;
    const iconv_t handle = iconv_open("UTF-8", name);
    if (handle != reinterpret_cast<iconv_t>(-1)) return Transcoder{encoding, handle};
  }
  return fail(ErrorKind::unsupported, "iconv does not know the charset '{}'", encoding.name);
#else
  return fail(ErrorKind::unsupported,
              "this build cannot decode '{}' (built without iconv); only UTF-8 and UTF-16 dictionaries open",
              encoding.name);
#endif
}

std::string Transcoder::to_utf8(ByteView bytes) {
  switch (encoding_.charset) {
    case Charset::utf8: return std::string(as_chars(bytes));
    case Charset::utf16le: return unicode::utf16le_to_utf8(bytes);
    case Charset::latin1: {
      std::string out;
      out.reserve(bytes.size());
      for (const std::byte b : bytes) unicode::append_utf8(out, std::to_integer<char32_t>(b));
      return out;
    }
    default: break;
  }
#if MINEDICT_HAVE_ICONV
  std::string out;
  out.reserve(bytes.size() * 3 / 2 + 8);
  std::string chunk(4096, '\0');
  const auto handle = static_cast<iconv_t>(handle_);
  iconv(handle, nullptr, nullptr, nullptr, nullptr);  // reset shift state

  // iconv wants a non-const char**; we never write through it.
  char* in = const_cast<char*>(reinterpret_cast<const char*>(bytes.data()));
  std::size_t in_left = bytes.size();
  while (in_left > 0) {
    char* out_ptr = chunk.data();
    std::size_t out_left = chunk.size();
    const std::size_t rc = iconv(handle, &in, &in_left, &out_ptr, &out_left);
    out.append(chunk.data(), chunk.size() - out_left);
    if (rc != static_cast<std::size_t>(-1)) continue;
    if (errno == E2BIG) continue;
    // EILSEQ: an undecodable byte; EINVAL: a truncated sequence at the end.
    unicode::append_utf8(out, unicode::replacement_character);
    if (errno == EINVAL) break;
    ++in;
    --in_left;
  }
  return out;
#else
  return std::string(as_chars(bytes));
#endif
}

}  // namespace minedict
