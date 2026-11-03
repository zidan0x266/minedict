// minedict - BSD 3-Clause License; see README.
#include "minedict/cli/dictionary.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <utility>

#include "minedict/cli/text.hpp"

namespace minedict::cli {
namespace {

bool has_extension(const std::filesystem::path& path, std::string_view wanted) {
  return text::to_lower_ascii(path.extension().string()) == wanted;
}

std::string clean_definition(std::string definition) {
  while (!definition.empty() && (definition.back() == '\r' || definition.back() == '\n' || definition.back() == ' ')) {
    definition.pop_back();
  }
  if (definition.starts_with("\xEF\xBB\xBF")) definition.erase(0, 3);
  return definition;
}

std::string display_title(const Header& header, const std::filesystem::path& path) {
  const std::string title = strip_tags(header.title);
  if (title.empty() || title == "Title (No HTML code allowed)") return path.stem().string();
  return title;
}

}  // namespace

// ---------------------------------------------------------------------------

struct Dictionary::Impl {
  Archive archive;
  Index index;
  Metadata meta;
  mutable std::optional<css::Sheet> stylesheet;
  mutable std::vector<std::filesystem::path> stylesheet_files;

  explicit Impl(Archive opened) : archive(std::move(opened)) {}

  [[nodiscard]] Result<std::string> definition(std::uint32_t position) const {
    if (position >= archive.size()) return fail(ErrorKind::invalid_argument, "key position out of range");
    auto text = archive.text(position);
    if (!text) return std::unexpected(text.error());
    return meta.header.stylesheet.apply(clean_definition(std::move(*text)));
  }

  Result<void> resolve(std::string_view word, const LookupOptions& options, unsigned depth,
                       std::vector<std::string>& chain, std::set<std::string>& visited,
                       std::vector<Entry>& out) const {
    for (const std::uint32_t position : index.exact(word)) {
      auto body = definition(position);
      if (!body) return std::unexpected(body.error());
      const std::string_view headword = archive.keys()[position].text;

      if (options.follow_links && body->starts_with(link_prefix)) {
        const std::string target{text::trim(std::string_view{*body}.substr(link_prefix.size()))};
        if (!target.empty() && depth < options.max_hops && !visited.contains(target)) {
          visited.insert(target);
          chain.emplace_back(headword);
          const auto status = resolve(target, options, depth + 1, chain, visited, out);
          chain.pop_back();
          if (!status) return status;
          continue;
        }
      }
      out.push_back(Entry{.headword = std::string(headword), .definition = std::move(*body), .via = chain});
    }
    return {};
  }
};

// ---------------------------------------------------------------------------

Dictionary::Dictionary(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Dictionary::Dictionary(Dictionary&&) noexcept = default;
Dictionary& Dictionary::operator=(Dictionary&&) noexcept = default;
Dictionary::~Dictionary() = default;

Result<Dictionary> Dictionary::open(const std::filesystem::path& path) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec)) return fail(ErrorKind::io, "{}: no such file", path.string());
  if (!has_extension(path, ".mdx") && !has_extension(path, ".mdd")) {
    return fail(ErrorKind::unsupported, "{}: expected a .mdx or .mdd file", path.string());
  }
  auto archive = Archive::open(path);
  if (!archive) return std::unexpected(archive.error());

  auto impl = std::make_unique<Impl>(std::move(*archive));
  const Header& header = impl->archive.header();
  const bool mdx = header.kind == Kind::mdx;
  impl->index = Index{impl->archive.keys(),
                      KeyRules{.case_sensitive = header.case_sensitive, .strip_punctuation = mdx && header.strip_key}};
  impl->meta = Metadata{.kind = header.kind,
                        .path = path,
                        .file_size = impl->archive.file_size(),
                        .entries = impl->archive.size(),
                        .title = display_title(header, path),
                        .header = header};
  return Dictionary{std::move(impl)};
}

const Metadata& Dictionary::metadata() const noexcept { return impl_->meta; }
const Archive& Dictionary::archive() const noexcept { return impl_->archive; }
const Index& Dictionary::index() const noexcept { return impl_->index; }
KeyRules Dictionary::key_rules() const noexcept { return impl_->index.rules(); }
std::size_t Dictionary::size() const noexcept { return impl_->archive.size(); }

KeyRef Dictionary::key_at(std::uint32_t position) const noexcept {
  const auto keys = impl_->archive.keys();
  if (position >= keys.size()) return {};
  return KeyRef{.key = keys[position].text, .position = position, .record_offset = keys[position].record_offset};
}

Result<std::string> Dictionary::definition_at(std::uint32_t position) const { return impl_->definition(position); }

Result<std::vector<Entry>> Dictionary::lookup(std::string_view word, const LookupOptions& options) const {
  std::vector<Entry> found;
  std::vector<std::string> chain;
  std::set<std::string> visited{std::string(word)};
  if (const auto status = impl_->resolve(word, options, 0, chain, visited, found); !status) {
    return std::unexpected(status.error());
  }
  std::vector<Entry> unique;
  for (Entry& entry : found) {
    const bool duplicate = std::ranges::any_of(unique, [&](const Entry& seen) {
      return seen.headword == entry.headword && seen.definition == entry.definition;
    });
    if (!duplicate) unique.push_back(std::move(entry));
  }
  return unique;
}

std::vector<KeyRef> Dictionary::find_prefix(std::string_view prefix, std::size_t limit) const {
  std::vector<KeyRef> out;
  for (const std::uint32_t position : impl_->index.prefix(prefix, limit)) out.push_back(key_at(position));
  return out;
}

std::vector<KeyRef> Dictionary::find_containing(std::string_view needle, std::size_t limit) const {
  std::vector<KeyRef> out;
  for (const std::uint32_t position : impl_->index.containing(needle, limit)) out.push_back(key_at(position));
  return out;
}

std::vector<Suggestion> Dictionary::suggest(std::string_view word, std::size_t limit, std::size_t max_distance) const {
  std::vector<Suggestion> out;
  for (const FuzzyMatch& match : impl_->index.fuzzy(word, limit, max_distance)) {
    out.push_back(Suggestion{.key = impl_->archive.keys()[match.position].text,
                             .distance = match.distance,
                             .position = match.position});
  }
  return out;
}

Result<std::vector<std::byte>> Dictionary::resource(std::string_view name) const {
  if (impl_->meta.kind != Kind::mdd) {
    return fail(ErrorKind::unsupported, "{} is not an MDD resource file", impl_->meta.path.string());
  }
  std::vector<std::string> candidates{std::string(name)};
  std::string backslashed(name);
  std::ranges::replace(backslashed, '/', '\\');
  if (backslashed != candidates.front()) candidates.push_back(backslashed);
  if (!backslashed.starts_with('\\')) candidates.push_back("\\" + backslashed);

  for (const std::string& candidate : candidates) {
    const auto positions = impl_->index.exact(candidate);
    if (positions.empty()) continue;
    return impl_->archive.record(positions.front());
  }
  return fail(ErrorKind::not_found, "no resource named '{}'", name);
}

const css::Sheet* Dictionary::stylesheet() const {
  if (!impl_->stylesheet) {
    std::error_code ec;
    const std::filesystem::path directory = impl_->meta.path.parent_path();
    std::vector<std::filesystem::path> files;
    const std::filesystem::path sibling = directory / (impl_->meta.path.stem().string() + ".css");
    if (std::filesystem::is_regular_file(sibling, ec)) {
      files.push_back(sibling);
    } else {
      for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (entry.is_regular_file(ec) && has_extension(entry.path(), ".css")) files.push_back(entry.path());
      }
      std::ranges::sort(files);
    }
    std::string source;
    for (const auto& file : files) {
      std::ifstream in(file, std::ios::binary);
      source.append(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
      source += '\n';
    }
    impl_->stylesheet_files = std::move(files);
    impl_->stylesheet = css::Sheet::parse(source);
  }
  return impl_->stylesheet->empty() ? nullptr : &*impl_->stylesheet;
}

const std::vector<std::filesystem::path>& Dictionary::stylesheet_files() const {
  (void)stylesheet();
  return impl_->stylesheet_files;
}

// ---------------------------------------------------------------------------

std::string strip_tags(std::string_view html) {
  std::string out;
  bool in_tag = false;
  for (const char c : html) {
    if (c == '<') {
      in_tag = true;
    } else if (c == '>') {
      in_tag = false;
    } else if (!in_tag) {
      out += c;
    }
  }
  return std::string(text::trim(text::decode_entities(out)));
}

std::string_view mime_type_for(std::string_view name) noexcept {
  struct Mapping {
    std::string_view extension;
    std::string_view mime;
  };
  constexpr std::array kTypes{
      Mapping{".png", "image/png"},     Mapping{".jpg", "image/jpeg"},    Mapping{".jpeg", "image/jpeg"},
      Mapping{".gif", "image/gif"},     Mapping{".bmp", "image/bmp"},     Mapping{".webp", "image/webp"},
      Mapping{".svg", "image/svg+xml"}, Mapping{".ico", "image/x-icon"},  Mapping{".mp3", "audio/mpeg"},
      Mapping{".ogg", "audio/ogg"},     Mapping{".wav", "audio/wav"},     Mapping{".spx", "audio/ogg"},
      Mapping{".m4a", "audio/mp4"},     Mapping{".mp4", "video/mp4"},     Mapping{".css", "text/css"},
      Mapping{".js", "application/javascript"}, Mapping{".html", "text/html"}, Mapping{".txt", "text/plain"},
      Mapping{".ttf", "font/ttf"},      Mapping{".otf", "font/otf"},      Mapping{".woff", "font/woff"},
      Mapping{".woff2", "font/woff2"},
  };
  const std::size_t dot = name.find_last_of('.');
  if (dot == std::string_view::npos) return "application/octet-stream";
  const std::string extension = text::to_lower_ascii(name.substr(dot));
  for (const Mapping& mapping : kTypes) {
    if (mapping.extension == extension) return mapping.mime;
  }
  return "application/octet-stream";
}

}  // namespace minedict::cli
