// minedict - BSD 3-Clause License; see README.
#include "minedict/cli/render.hpp"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <format>
#include <map>
#include <optional>
#include <ranges>
#include <vector>

#include "minedict/cli/compat.hpp"
#include "minedict/cli/css.hpp"
#include "minedict/cli/html.hpp"
#include "minedict/cli/text.hpp"
#include "minedict/unicode.hpp"

namespace minedict::cli {
namespace {

enum class Transform { none, superscript, subscript };

struct Frame {
  std::string tag;
  std::vector<std::string> classes;
  std::string id;
  std::vector<html::Attribute> attributes;
  Style style;  // effective style, already layered over the parents
  bool hidden{false};     // display: none (nothing inside renders, pseudo-elements included)
  bool invisible{false};  // visibility: hidden (a ::before/::after may still be visible)
  bool preformatted{false};
  bool block{false};
  bool space_after{false};
  bool blank_after{false};  // a blank line once the block closes
  Transform transform{Transform::none};
  bool list{false};
  bool ordered{false};
  int counter{0};
  std::optional<css::Declarations> after;  // a matching ::after rule, applied on close
  Style after_style;
  std::vector<std::pair<std::string, std::optional<int>>> saved_counters;  // restored on close
};

/// ASCII whitespace plus the typographic spaces (en, em, thin, ...); the
/// no-break space is not included.
constexpr bool is_collapsible_space(char32_t cp) noexcept {
  return cp == U' ' || cp == U'\t' || cp == U'\n' || cp == U'\r' || cp == U'\f' || cp == U'\v' ||
         (cp >= 0x2000 && cp <= 0x200A) || cp == 0x202F || cp == 0x205F || cp == 0x3000;
}

/// Private-use code points are icon-font glyphs a terminal cannot draw.
constexpr bool is_private_use(char32_t cp) noexcept {
  return (cp >= 0xE000 && cp <= 0xF8FF) || (cp >= 0xF0000 && cp <= 0x10FFFD);
}

struct Layout {
  std::string first_indent;
  std::string rest_indent;
};

/// Value of a `property` inside an inline `style="..."` attribute.
std::optional<std::string_view> css_property(std::string_view declarations, std::string_view property) {
  for (const auto part : std::views::split(declarations, ';')) {
    const std::string_view declaration{part.begin(), part.end()};
    const std::size_t colon = declaration.find(':');
    if (colon == std::string_view::npos) continue;
    if (text::iequals_ascii(text::trim(declaration.substr(0, colon)), property)) {
      return text::trim(declaration.substr(colon + 1));
    }
  }
  return std::nullopt;
}

bool css_is_hidden(std::string_view declarations) {
  if (const auto display = css_property(declarations, "display"); display && *display == "none") return true;
  if (const auto visibility = css_property(declarations, "visibility"); visibility && *visibility == "hidden") {
    return true;
  }
  return false;
}

bool is_heading(std::string_view name) noexcept {
  return name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6';
}

class Builder {
 public:
  Builder(const Theme& theme, const RenderOptions& options) : theme_(theme), options_(options) {}

  std::string run(std::string_view html) {
    for (const auto& token : html::tokenize(html)) {
      std::visit(overloaded{
                     [this](const html::Text& t) { on_text(t.content); },
                     [this](const html::Tag& t) { t.closing ? on_close(t) : on_open(t); },
                     [](const html::Comment&) {},
                 },
                 token);
    }
    flush();
    return join_lines();
  }

 private:
  // ---- state helpers -------------------------------------------------------

  [[nodiscard]] const Style& current_style() const noexcept {
    return frames_.empty() ? theme_[Role::body] : frames_.back().style;
  }
  [[nodiscard]] bool hidden() const noexcept {
    return !frames_.empty() && (frames_.back().hidden || frames_.back().invisible);
  }
  [[nodiscard]] bool display_none() const noexcept { return !frames_.empty() && frames_.back().hidden; }
  [[nodiscard]] bool preformatted() const noexcept { return !frames_.empty() && frames_.back().preformatted; }
  [[nodiscard]] Transform transform() const noexcept {
    return frames_.empty() ? Transform::none : frames_.back().transform;
  }
  [[nodiscard]] std::size_t nesting_columns() const noexcept {
    std::size_t columns = 0;
    for (const Frame& frame : frames_) {
      if (frame.list || frame.tag == "blockquote" || frame.tag == "dd") columns += 2;
    }
    return columns;
  }
  [[nodiscard]] Frame* innermost_list() noexcept {
    const auto it = std::ranges::find_if(frames_ | std::views::reverse, &Frame::list);
    return it == frames_.rend() ? nullptr : &*it;
  }
  [[nodiscard]] std::string styled(std::string_view s, const Style& style) const {
    return paint(s, style, options_.color);
  }

  // ---- stylesheet lookups ------------------------------------------------------

  [[nodiscard]] static css::Element element_of(const Frame& frame) {
    css::Element element{.tag = frame.tag, .classes = {}, .id = frame.id, .attributes = &frame.attributes};
    element.classes.reserve(frame.classes.size());
    for (const std::string& name : frame.classes) element.classes.emplace_back(name);
    return element;
  }

  /// Declarations for `frame` as the child of the current open frames.
  [[nodiscard]] css::Declarations resolve(const Frame& frame, css::Pseudo pseudo) const {
    css::Declarations result;
    if (options_.stylesheets.empty()) return result;
    std::vector<css::Element> path;
    path.reserve(frames_.size() + 1);
    for (const Frame& open : frames_) path.push_back(element_of(open));
    path.push_back(element_of(frame));
    for (const css::Sheet* sheet : options_.stylesheets) {
      if (sheet != nullptr) result.merge(sheet->resolve(path, pseudo));
    }
    return result;
  }

  [[nodiscard]] Style style_from(const css::Declarations& d, Style base) const {
    if (d.color) {
      if (const auto mapped = theme_.map_html_color(*d.color)) base.fg = *mapped;
    }
    if (d.bold) base.bold = *d.bold;
    if (d.italic) base.italic = *d.italic;
    if (d.underline) base.underline = *d.underline;
    if (d.strike) base.strike = *d.strike;
    return base;
  }

  /// A pseudo-element shows unless its element is display:none, or is
  /// invisible without the pseudo-element saying `visibility: visible`.
  [[nodiscard]] static bool pseudo_visible(const Frame& frame, const css::Declarations& pseudo) noexcept {
    if (frame.hidden) return false;
    if (pseudo.hidden) return !*pseudo.hidden;
    return !frame.invisible;
  }

  /// Run the counter-reset / counter-increment of `d`, scoped to `owner`.
  void apply_counters(const css::Declarations& d, Frame& owner) {
    for (const css::CounterAction& action : d.counter_reset) {
      const auto it = counters_.find(action.name);
      owner.saved_counters.emplace_back(action.name, it == counters_.end() ? std::nullopt : std::optional(it->second));
      counters_[action.name] = action.value;
    }
    for (const css::CounterAction& action : d.counter_increment) counters_[action.name] += action.value;
  }

  void restore_counters(const Frame& frame) {
    for (auto it = frame.saved_counters.rbegin(); it != frame.saved_counters.rend(); ++it) {
      if (it->second) counters_[it->first] = *it->second;
      else counters_.erase(it->first);
    }
  }

  [[nodiscard]] std::string content_text(const std::optional<std::vector<css::ContentPart>>& content) const {
    std::string out;
    if (!content) return out;
    for (const css::ContentPart& part : *content) {
      if (!part.counter.empty()) {
        const auto it = counters_.find(part.counter);
        out += std::to_string(it == counters_.end() ? 0 : it->second);
      } else {
        out += strip_private_use(part.text);
      }
    }
    return out;
  }

  [[nodiscard]] static std::string strip_private_use(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
      const auto [cp, n] = unicode::decode_utf8(s, i);
      if (!is_private_use(cp)) out.append(s.substr(i, n));
      i += n;
    }
    return out;
  }

  /// A word boundary implied by CSS spacing rather than by whitespace in the
  /// text; it disappears in front of closing punctuation.
  void soft_space() {
    if (!line_has_text_ || pending_space_) return;
    pending_space_ = true;
    pending_space_soft_ = true;
  }

  static constexpr bool is_closing_punctuation(char32_t cp) noexcept {
    return cp == U'.' || cp == U',' || cp == U';' || cp == U':' || cp == U'!' || cp == U'?' || cp == U')' ||
           cp == U']' || cp == U'}' || cp == U'\u3002' || cp == U'\uFF0C' || cp == U'\uFF1B' || cp == U'\uFF1A' ||
           cp == U'\uFF01' || cp == U'\uFF1F' || cp == U'\uFF09';
  }

  void apply_stylesheet(const css::Declarations& d, Frame& frame) {
    if (d.display == css::Display::none) frame.hidden = true;
    if (d.hidden) frame.invisible = *d.hidden;
    if (d.display == css::Display::block) frame.block = true;
    if (d.display == css::Display::inline_ || d.display == css::Display::inline_block) frame.block = false;
    if (d.preformatted) frame.preformatted = *d.preformatted;
    frame.style = style_from(d, frame.style);
    const bool boxed = d.display == css::Display::inline_block;
    if (boxed || d.space_before.value_or(false)) soft_space();
    frame.space_after = boxed || d.space_after.value_or(false);
    const bool is_block = frame.block || html::is_block_element(frame.tag);
    if (is_block && d.blank_before.value_or(false)) {
      flush();
      request_blank_line();
    }
    frame.blank_after = is_block && d.blank_after.value_or(false);
  }

  // ---- paragraph assembly ----------------------------------------------------

  void emit(std::string_view s, const Style& style) {
    if (s.empty()) return;
    paragraph_ += styled(s, style);
    line_has_text_ = true;
    cell_has_text_ = true;
  }

  void emit_with_spacing(std::string_view s, const Style& style) {
    if (s.empty()) return;
    if (pending_space_ && line_has_text_ && !(pending_space_soft_ && is_closing_punctuation(unicode::decode_utf8(s, 0).code_point))) {
      paragraph_ += ' ';
    }
    pending_space_ = false;
    pending_space_soft_ = false;
    emit(s, style);
  }

  void on_text(std::string_view raw) {
    if (hidden()) return;
    const std::string decoded = text::decode_entities(raw);
    const Style& style = current_style();

    if (preformatted()) {
      std::size_t start = 0;
      while (true) {
        const std::size_t newline = decoded.find('\n', start);
        emit(std::string_view{decoded}.substr(start, newline == std::string::npos ? std::string::npos : newline - start),
             style);
        if (newline == std::string::npos) break;
        hard_break();
        start = newline + 1;
      }
      return;
    }

    std::string buffer;
    for (std::size_t i = 0; i < decoded.size();) {
      const auto [cp, n] = unicode::decode_utf8(decoded, i);
      i += n;
      if (is_collapsible_space(cp)) {
        pending_space_ = true;
        pending_space_soft_ = false;
        continue;
      }
      if (is_private_use(cp)) continue;
      if (pending_space_ && (line_has_text_ || !buffer.empty()) &&
          !(pending_space_soft_ && is_closing_punctuation(cp))) {
        buffer += ' ';
      }
      pending_space_ = false;
      pending_space_soft_ = false;
      unicode::append_utf8(buffer, cp);
    }
    switch (transform()) {
      case Transform::superscript: buffer = text::superscript(buffer); break;
      case Transform::subscript: buffer = text::subscript(buffer); break;
      case Transform::none: break;
    }
    emit(buffer, style);
  }

  void hard_break() {
    paragraph_ += '\n';
    line_has_text_ = false;
    pending_space_ = false;
    pending_space_soft_ = false;
  }

  void request_blank_line() { want_blank_ = true; }

  void flush() {
    const bool has_content = !text::trim(text::strip_ansi(paragraph_)).empty();
    if (has_content) {
      if (want_blank_ && !lines_.empty()) lines_.emplace_back();
      want_blank_ = false;
      const Layout layout = pending_layout_.value_or(default_layout());
      for (auto& line : text::wrap(paragraph_, options_.width, layout.first_indent, layout.rest_indent)) {
        lines_.push_back(std::move(line));
      }
    }
    paragraph_.clear();
    if (has_content) pending_layout_.reset();  // keep an <li> layout until its text arrives
    pending_space_ = false;
    line_has_text_ = false;
  }

  [[nodiscard]] Layout default_layout() const {
    std::string indent(options_.indent + nesting_columns(), ' ');
    return Layout{.first_indent = indent, .rest_indent = indent};
  }

  void push_rule() {
    flush();
    const std::size_t indent = options_.indent + nesting_columns();
    const std::size_t columns = options_.width > indent + 1 ? options_.width - indent : 1;
    lines_.push_back(std::string(indent, ' ') + styled(text::repeat("─", columns), theme_[Role::rule]));
  }

  [[nodiscard]] std::string join_lines() const {
    // Collapse runs of blank lines and trim blank lines at both ends.
    std::vector<std::string_view> kept;
    kept.reserve(lines_.size());
    for (const std::string& line : lines_) {
      const bool blank = text::trim(text::strip_ansi(line)).empty();
      if (blank) {
        if (kept.empty() || kept.back().empty()) continue;
        kept.emplace_back();
      } else {
        kept.emplace_back(line);
      }
    }
    while (!kept.empty() && kept.back().empty()) kept.pop_back();

    std::string out;
    for (std::size_t i = 0; i < kept.size(); ++i) {
      if (i > 0) out += '\n';
      out += kept[i];
    }
    return out;
  }

  // ---- tags ----------------------------------------------------------------------

  void implied_close(std::string_view name) {
    if (std::ranges::any_of(frames_, [&](const Frame& f) { return f.tag == name; })) {
      close_frame(name);
    }
  }

  void on_open(const html::Tag& tag) {
    const std::string_view name = tag.name;

    // Void elements never push a frame.
    if (name == "br") {
      if (!hidden()) hard_break();
      return;
    }
    if (name == "hr") {
      if (!hidden()) push_rule();
      return;
    }
    if (name == "img") {
      if (!hidden() && options_.show_images) on_image(tag);
      return;
    }
    if (html::is_void_element(name)) return;

    // Elements that implicitly close an open sibling.
    if (name == "li") implied_close_list_item();
    if (name == "p") implied_close("p");
    if (name == "td" || name == "th") {
      implied_close("td");
      implied_close("th");
    }
    if (name == "tr") {
      implied_close("td");
      implied_close("th");
      implied_close("tr");
    }
    if (name == "dt" || name == "dd") {
      implied_close("dt");
      implied_close("dd");
    }

    Frame frame{.tag = std::string(name),
                .classes = {},
                .id = std::string(tag.attribute("id").value_or("")),
                .attributes = tag.attributes,
                .style = current_style(),
                .hidden = display_none(),
                .invisible = !frames_.empty() && frames_.back().invisible,
                .preformatted = preformatted(),
                .block = html::is_block_element(name),
                .transform = transform()};
    for (const std::string_view class_name : css::class_names(tag.attribute("class").value_or(""))) {
      frame.classes.emplace_back(class_name);
    }

    // Cascade: the dictionary's stylesheet, then inline attributes, then the
    // element's own meaning.
    const css::Declarations declarations = resolve(frame, css::Pseudo::none);
    const css::Declarations before = resolve(frame, css::Pseudo::before);
    const css::Declarations after = resolve(frame, css::Pseudo::after);
    apply_stylesheet(declarations, frame);
    apply_attributes(tag, frame);
    apply_semantics(tag, frame);
    apply_counters(declarations, frame);
    apply_counters(before, frame);
    const std::string before_text = content_text(before.content);
    if (!after.empty()) {
      frame.after = after;
      frame.after_style = style_from(after, frame.style);
    }
    frames_.push_back(std::move(frame));

    if (!before_text.empty() && pseudo_visible(frames_.back(), before)) {
      if (before.space_before.value_or(false)) soft_space();
      emit_with_spacing(before_text, style_from(before, current_style()));
      if (before.space_after.value_or(true)) soft_space();
    }

    if (tag.self_closing) close_frame(name);
  }

  void apply_attributes(const html::Tag& tag, Frame& frame) {
    if (const auto colour = tag.attribute("color")) {
      if (const auto mapped = theme_.map_html_color(*colour)) frame.style.fg = *mapped;
    }
    if (const auto style_attr = tag.attribute("style")) {
      if (css_is_hidden(*style_attr)) frame.hidden = true;
      if (const auto colour = css_property(*style_attr, "color")) {
        if (const auto mapped = theme_.map_html_color(*colour)) frame.style.fg = *mapped;
      }
      if (const auto weight = css_property(*style_attr, "font-weight")) {
        int numeric = 0;
        const auto [ptr, ec] = std::from_chars(weight->data(), weight->data() + weight->size(), numeric);
        if (*weight == "bold" || *weight == "bolder" || (ec == std::errc{} && numeric >= 600)) {
          frame.style.bold = true;
        }
      }
      if (const auto font_style = css_property(*style_attr, "font-style");
          font_style && (*font_style == "italic" || *font_style == "oblique")) {
        frame.style.italic = true;
      }
      if (const auto decoration = css_property(*style_attr, "text-decoration")) {
        if (decoration->contains("underline")) frame.style.underline = true;
        if (decoration->contains("line-through")) frame.style.strike = true;
      }
      if (const auto display = css_property(*style_attr, "display")) {
        if (*display == "block") frame.block = true;
        if (display->starts_with("inline")) frame.block = false;
      }
    }
    if (tag.attribute("hidden")) frame.hidden = true;
  }

  void apply_semantics(const html::Tag& tag, Frame& frame) {
    const std::string_view name = frame.tag;
    const auto layer = [&](Role role) { frame.style = frame.style.layered(theme_[role]); };

    if (name == "b" || name == "strong" || name == "th" || name == "dt") {
      layer(Role::strong);
    } else if (name == "i" || name == "em" || name == "cite" || name == "dfn" || name == "var" || name == "q") {
      layer(Role::emphasis);
    } else if (name == "u" || name == "ins") {
      frame.style.underline = true;
    } else if (name == "s" || name == "strike" || name == "del") {
      frame.style.strike = true;
    } else if (name == "small") {
      frame.style.dim = true;
    } else if (name == "code" || name == "tt" || name == "kbd" || name == "samp") {
      layer(Role::code);
    } else if (name == "mark") {
      layer(Role::highlight);
    } else if (name == "sup") {
      frame.transform = Transform::superscript;
    } else if (name == "sub") {
      frame.transform = Transform::subscript;
    } else if (name == "a") {
      on_anchor(tag, frame);
    } else if (name == "script" || name == "style" || name == "head" || name == "title" || name == "noscript" ||
               name == "template" || name == "link" || name == "meta") {
      frame.hidden = true;
    } else if (name == "pre") {
      frame.preformatted = true;
    } else if (name == "ul" || name == "ol" || name == "menu" || name == "dir") {
      frame.list = true;
      frame.ordered = name == "ol";
    }

    if (is_heading(name)) {
      layer(Role::heading);
      flush();
      request_blank_line();
      return;
    }
    if (name == "li") {
      flush();
      begin_list_item();
      return;
    }
    if (name == "tr") {
      flush();
      row_has_cell_ = false;
      return;
    }
    if (name == "td" || name == "th") {
      begin_cell();
      return;
    }
    if (frame.block) {
      flush();
    }
  }

  void on_anchor(const html::Tag& tag, Frame& frame) {
    const auto href = tag.attribute("href").value_or("");
    if (href.starts_with("sound://")) {
      if (options_.show_sounds) {
        emit_with_spacing("♪", theme_[Role::resource]);
        pending_space_ = true;
      }
      frame.hidden = true;  // the link body is usually a speaker icon
      return;
    }
    if (href.starts_with("entry://") || href.starts_with("http://") || href.starts_with("https://") ||
        href.starts_with("bword://")) {
      frame.style = frame.style.layered(theme_[Role::link]);
    }
  }

  void on_image(const html::Tag& tag) {
    Frame probe{.tag = "img", .classes = {}, .id = std::string(tag.attribute("id").value_or("")), .attributes = tag.attributes};
    for (const std::string_view class_name : css::class_names(tag.attribute("class").value_or(""))) {
      probe.classes.emplace_back(class_name);
    }
    const css::Declarations declarations = resolve(probe, css::Pseudo::none);
    if (declarations.display == css::Display::none || declarations.hidden.value_or(false)) return;
    if (const auto style_attr = tag.attribute("style"); style_attr && css_is_hidden(*style_attr)) return;

    const std::string_view alt = text::trim(tag.attribute("alt").value_or(""));
    const std::string_view src = tag.attribute("src").value_or("");
    std::string label;
    if (!alt.empty()) {
      label = std::format("[{}]", alt);
    } else if (!src.empty()) {
      const std::string name = std::filesystem::path(std::string(src)).filename().string();
      label = std::format("[img: {}]", name.empty() ? std::string(src) : name);
    } else {
      label = "[img]";
    }
    const bool block = declarations.display == css::Display::block;
    if (block) flush();
    emit_with_spacing(label, theme_[Role::resource]);
    if (block) flush();
  }

  void implied_close_list_item() {
    // An open <li> under the innermost list is closed by the next <li>.
    for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
      if (it->list) break;
      if (it->tag == "li") {
        close_frame("li");
        break;
      }
    }
  }

  void begin_list_item() {
    Frame* list = innermost_list();
    std::string marker;
    if (list && list->ordered) {
      marker = std::format("{}. ", ++list->counter);
    } else {
      marker = "• ";
    }
    const std::size_t marker_width = text::display_width(marker);
    // The list's own two columns host the marker.
    const std::size_t nesting = nesting_columns();
    const std::size_t base = options_.indent + (nesting >= 2 ? nesting - 2 : 0);
    pending_layout_ = Layout{
        .first_indent = std::string(base, ' ') + styled(marker, theme_[Role::bullet]),
        .rest_indent = std::string(base + marker_width, ' '),
    };
  }

  void begin_cell() {
    cell_mark_ = paragraph_.size();
    if (row_has_cell_) paragraph_ += styled(" │ ", theme_[Role::table_border]);
    cell_text_start_ = paragraph_.size();
    cell_has_text_ = false;
    pending_space_ = false;
  }

  [[nodiscard]] bool cell_is_blank() const {
    if (!cell_has_text_) return true;
    const std::string content = text::strip_ansi(std::string_view{paragraph_}.substr(cell_text_start_));
    for (std::size_t i = 0; i < content.size();) {
      const auto [cp, n] = unicode::decode_utf8(content, i);
      if (!unicode::is_space(cp)) return false;  // non-breaking spaces count as blank too
      i += n;
    }
    return true;
  }

  void end_cell() {
    if (cell_is_blank()) {
      paragraph_.resize(cell_mark_);  // drop empty cells and their separator
    } else {
      row_has_cell_ = true;
    }
    pending_space_ = false;
  }

  void on_close(const html::Tag& tag) { close_frame(tag.name); }

  void close_frame(std::string_view name) {
    const auto it = std::ranges::find(frames_ | std::views::reverse, name, &Frame::tag);
    if (it == frames_.rend()) return;  // stray closing tag

    // Close everything above the match first (unclosed children).
    while (frames_.back().tag != name) {
      finish_frame(frames_.back());
      frames_.pop_back();
    }
    finish_frame(frames_.back());
    frames_.pop_back();
  }

  void finish_frame(Frame& frame) {
    const std::string_view name = frame.tag;
    if (frame.after) {
      apply_counters(*frame.after, frame);
      const std::string after_text = content_text(frame.after->content);
      if (!after_text.empty() && pseudo_visible(frame, *frame.after)) {
        if (frame.after->space_before.value_or(false)) soft_space();
        emit_with_spacing(after_text, frame.after_style);
        if (frame.after->space_after.value_or(false)) soft_space();
      }
    }
    restore_counters(frame);
    if (name == "td" || name == "th") {
      end_cell();
      return;
    }
    if (is_heading(name)) {
      flush();
      request_blank_line();
      return;
    }
    if (name == "li") {
      flush();
      pending_layout_.reset();
      return;
    }
    if (frame.block) {
      flush();
      if (frame.blank_after) request_blank_line();
      return;
    }
    if (frame.space_after) soft_space();
  }

  // ---- members ---------------------------------------------------------------

  const Theme& theme_;
  const RenderOptions& options_;
  std::vector<Frame> frames_;
  std::map<std::string, int> counters_;
  std::vector<std::string> lines_;
  std::string paragraph_;
  std::optional<Layout> pending_layout_;
  bool pending_space_{false};
  bool pending_space_soft_{false};
  bool line_has_text_{false};
  bool want_blank_{false};
  bool row_has_cell_{false};
  bool cell_has_text_{false};
  std::size_t cell_mark_{0};
  std::size_t cell_text_start_{0};
};

}  // namespace

std::string HtmlRenderer::render(std::string_view html) const {
  Builder builder{*theme_, options_};
  return builder.run(html);
}

std::string html_to_plain_text(std::string_view html) {
  static const Theme plain = Theme::plain();
  const RenderOptions options{.width = 100'000, .indent = 0, .color = ColorMode::none, .show_images = false,
                              .show_sounds = false, .stylesheets = {}};
  return HtmlRenderer{plain, options}.render(html);
}

}  // namespace minedict::cli
