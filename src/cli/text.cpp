// minedict - BSD 3-Clause License; see README.
#include "minedict/cli/text.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <optional>
#include <ranges>
#include <utility>

#include "minedict/cli/ansi.hpp"

namespace minedict::cli::text {

using unicode::append_utf8;
using unicode::decode_utf8;

namespace {

struct Range {
  char32_t first;
  char32_t last;
};

constexpr bool in_ranges(char32_t cp, const auto& ranges) noexcept {
  return std::ranges::any_of(ranges, [cp](const Range& r) { return cp >= r.first && cp <= r.last; });
}

// Combining marks and other zero-width code points (abridged).
constexpr std::array kZeroWidth{
    Range{0x0300, 0x036F}, Range{0x0483, 0x0489}, Range{0x0591, 0x05BD}, Range{0x05BF, 0x05C7},
    Range{0x0610, 0x061A}, Range{0x064B, 0x065F}, Range{0x0670, 0x0670}, Range{0x06D6, 0x06DC},
    Range{0x06DF, 0x06E4}, Range{0x0E31, 0x0E31}, Range{0x0E34, 0x0E3A}, Range{0x0E47, 0x0E4E},
    Range{0x1AB0, 0x1AFF}, Range{0x1DC0, 0x1DFF}, Range{0x200B, 0x200F}, Range{0x2028, 0x202E},
    Range{0x2060, 0x2064}, Range{0x20D0, 0x20FF}, Range{0x3099, 0x309A}, Range{0xFE00, 0xFE0F},
    Range{0xFE20, 0xFE2F}, Range{0xFEFF, 0xFEFF}, Range{0xE0100, 0xE01EF},
};

// East Asian wide/fullwidth and common emoji blocks.
constexpr std::array kWide{
    Range{0x1100, 0x115F},   Range{0x231A, 0x231B},   Range{0x2329, 0x232A},   Range{0x23E9, 0x23EC},
    Range{0x25FD, 0x25FE},   Range{0x2614, 0x2615},   Range{0x2648, 0x2653},   Range{0x267F, 0x267F},
    Range{0x2693, 0x2693},   Range{0x26A1, 0x26A1},   Range{0x26AA, 0x26AB},   Range{0x26BD, 0x26BE},
    Range{0x26C4, 0x26C5},   Range{0x26CE, 0x26CE},   Range{0x26D4, 0x26D4},   Range{0x26EA, 0x26EA},
    Range{0x26F2, 0x26F3},   Range{0x26F5, 0x26F5},   Range{0x26FA, 0x26FA},   Range{0x26FD, 0x26FD},
    Range{0x2705, 0x2705},   Range{0x270A, 0x270B},   Range{0x2728, 0x2728},   Range{0x274C, 0x274C},
    Range{0x274E, 0x274E},   Range{0x2753, 0x2755},   Range{0x2757, 0x2757},   Range{0x2795, 0x2797},
    Range{0x27B0, 0x27B0},   Range{0x27BF, 0x27BF},   Range{0x2B1B, 0x2B1C},   Range{0x2B50, 0x2B50},
    Range{0x2B55, 0x2B55},   Range{0x2E80, 0x303E},   Range{0x3041, 0x33FF},   Range{0x3400, 0x4DBF},
    Range{0x4E00, 0x9FFF},   Range{0xA000, 0xA4CF},   Range{0xA960, 0xA97F},   Range{0xAC00, 0xD7A3},
    Range{0xF900, 0xFAFF},   Range{0xFE10, 0xFE19},   Range{0xFE30, 0xFE6F},   Range{0xFF00, 0xFF60},
    Range{0xFFE0, 0xFFE6},   Range{0x16FE0, 0x16FE4}, Range{0x17000, 0x18AFF}, Range{0x1B000, 0x1B2FF},
    Range{0x1F004, 0x1F004}, Range{0x1F0CF, 0x1F0CF}, Range{0x1F18E, 0x1F18E}, Range{0x1F191, 0x1F19A},
    Range{0x1F200, 0x1F251}, Range{0x1F300, 0x1F64F}, Range{0x1F680, 0x1F6FF}, Range{0x1F7E0, 0x1F7EB},
    Range{0x1F90C, 0x1F9FF}, Range{0x1FA70, 0x1FAFF}, Range{0x20000, 0x2FFFD}, Range{0x30000, 0x3FFFD},
};

struct Entity {
  std::string_view name;
  char32_t code_point;
};

constexpr std::array kEntities{
    Entity{"amp", U'&'},       Entity{"lt", U'<'},        Entity{"gt", U'>'},        Entity{"quot", U'"'},
    Entity{"apos", U'\''},     Entity{"nbsp", U' '}, Entity{"ensp", U' '}, Entity{"emsp", U' '},
    Entity{"thinsp", U' '}, Entity{"zwnj", U'‌'}, Entity{"zwj", U'‍'}, Entity{"shy", U'­'},
    Entity{"copy", U'©'}, Entity{"reg", U'®'},  Entity{"trade", U'™'}, Entity{"deg", U'°'},
    Entity{"middot", U'·'}, Entity{"bull", U'•'}, Entity{"hellip", U'…'}, Entity{"ndash", U'–'},
    Entity{"mdash", U'—'}, Entity{"lsquo", U'‘'}, Entity{"rsquo", U'’'}, Entity{"sbquo", U'‚'},
    Entity{"ldquo", U'“'}, Entity{"rdquo", U'”'}, Entity{"bdquo", U'„'}, Entity{"laquo", U'«'},
    Entity{"raquo", U'»'}, Entity{"lsaquo", U'‹'}, Entity{"rsaquo", U'›'}, Entity{"times", U'×'},
    Entity{"divide", U'÷'}, Entity{"plusmn", U'±'}, Entity{"minus", U'−'}, Entity{"para", U'¶'},
    Entity{"sect", U'§'}, Entity{"dagger", U'†'}, Entity{"Dagger", U'‡'}, Entity{"permil", U'‰'},
    Entity{"prime", U'′'}, Entity{"Prime", U'″'}, Entity{"euro", U'€'}, Entity{"pound", U'£'},
    Entity{"yen", U'¥'},  Entity{"cent", U'¢'},  Entity{"curren", U'¤'}, Entity{"iexcl", U'¡'},
    Entity{"iquest", U'¿'}, Entity{"larr", U'←'}, Entity{"uarr", U'↑'}, Entity{"rarr", U'→'},
    Entity{"darr", U'↓'}, Entity{"harr", U'↔'},  Entity{"lArr", U'⇐'}, Entity{"rArr", U'⇒'},
    Entity{"hArr", U'⇔'}, Entity{"hearts", U'♥'}, Entity{"diams", U'♦'}, Entity{"clubs", U'♣'},
    Entity{"spades", U'♠'}, Entity{"loz", U'◊'}, Entity{"infin", U'∞'}, Entity{"ne", U'≠'},
    Entity{"le", U'≤'},   Entity{"ge", U'≥'},    Entity{"asymp", U'≈'}, Entity{"equiv", U'≡'},
    Entity{"radic", U'√'}, Entity{"sum", U'∑'},  Entity{"prod", U'∏'}, Entity{"part", U'∂'},
    Entity{"micro", U'µ'}, Entity{"frac12", U'½'}, Entity{"frac14", U'¼'}, Entity{"frac34", U'¾'},
    Entity{"sup1", U'¹'}, Entity{"sup2", U'²'},  Entity{"sup3", U'³'}, Entity{"ordf", U'ª'},
    Entity{"ordm", U'º'}, Entity{"macr", U'¯'},  Entity{"acute", U'´'}, Entity{"cedil", U'¸'},
    Entity{"uml", U'¨'},  Entity{"brvbar", U'¦'}, Entity{"not", U'¬'}, Entity{"szlig", U'ß'},
    Entity{"Agrave", U'À'}, Entity{"Aacute", U'Á'}, Entity{"Acirc", U'Â'}, Entity{"Atilde", U'Ã'},
    Entity{"Auml", U'Ä'}, Entity{"Aring", U'Å'},  Entity{"AElig", U'Æ'}, Entity{"Ccedil", U'Ç'},
    Entity{"Egrave", U'È'}, Entity{"Eacute", U'É'}, Entity{"Ecirc", U'Ê'}, Entity{"Euml", U'Ë'},
    Entity{"Igrave", U'Ì'}, Entity{"Iacute", U'Í'}, Entity{"Icirc", U'Î'}, Entity{"Iuml", U'Ï'},
    Entity{"Ntilde", U'Ñ'}, Entity{"Ograve", U'Ò'}, Entity{"Oacute", U'Ó'}, Entity{"Ocirc", U'Ô'},
    Entity{"Otilde", U'Õ'}, Entity{"Ouml", U'Ö'}, Entity{"Oslash", U'Ø'}, Entity{"Ugrave", U'Ù'},
    Entity{"Uacute", U'Ú'}, Entity{"Ucirc", U'Û'}, Entity{"Uuml", U'Ü'}, Entity{"Yacute", U'Ý'},
    Entity{"agrave", U'à'}, Entity{"aacute", U'á'}, Entity{"acirc", U'â'}, Entity{"atilde", U'ã'},
    Entity{"auml", U'ä'}, Entity{"aring", U'å'},  Entity{"aelig", U'æ'}, Entity{"ccedil", U'ç'},
    Entity{"egrave", U'è'}, Entity{"eacute", U'é'}, Entity{"ecirc", U'ê'}, Entity{"euml", U'ë'},
    Entity{"igrave", U'ì'}, Entity{"iacute", U'í'}, Entity{"icirc", U'î'}, Entity{"iuml", U'ï'},
    Entity{"ntilde", U'ñ'}, Entity{"ograve", U'ò'}, Entity{"oacute", U'ó'}, Entity{"ocirc", U'ô'},
    Entity{"otilde", U'õ'}, Entity{"ouml", U'ö'}, Entity{"oslash", U'ø'}, Entity{"ugrave", U'ù'},
    Entity{"uacute", U'ú'}, Entity{"ucirc", U'û'}, Entity{"uuml", U'ü'}, Entity{"yacute", U'ý'},
    Entity{"yuml", U'ÿ'}, Entity{"oelig", U'œ'},  Entity{"OElig", U'Œ'}, Entity{"scaron", U'š'},
    Entity{"Scaron", U'Š'}, Entity{"alpha", U'α'}, Entity{"beta", U'β'}, Entity{"gamma", U'γ'},
    Entity{"delta", U'δ'}, Entity{"epsilon", U'ε'}, Entity{"theta", U'θ'}, Entity{"lambda", U'λ'},
    Entity{"mu", U'μ'},   Entity{"pi", U'π'},    Entity{"sigma", U'σ'}, Entity{"omega", U'ω'},
    Entity{"Omega", U'Ω'}, Entity{"schwa", U'ə'},
};

// Windows-1252 code points for &#128;..&#159;, which HTML maps to the
// corresponding printable characters rather than C1 controls.
constexpr std::array<char32_t, 32> kCp1252{
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
    0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

struct ScriptMap {
  char32_t plain;
  char32_t sup;
  char32_t sub;
};

constexpr std::array kScripts{
    ScriptMap{U'0', U'⁰', U'₀'}, ScriptMap{U'1', U'¹', U'₁'},
    ScriptMap{U'2', U'²', U'₂'}, ScriptMap{U'3', U'³', U'₃'},
    ScriptMap{U'4', U'⁴', U'₄'}, ScriptMap{U'5', U'⁵', U'₅'},
    ScriptMap{U'6', U'⁶', U'₆'}, ScriptMap{U'7', U'⁷', U'₇'},
    ScriptMap{U'8', U'⁸', U'₈'}, ScriptMap{U'9', U'⁹', U'₉'},
    ScriptMap{U'+', U'⁺', U'₊'}, ScriptMap{U'-', U'⁻', U'₋'},
    ScriptMap{U'=', U'⁼', U'₌'}, ScriptMap{U'(', U'⁽', U'₍'},
    ScriptMap{U')', U'⁾', U'₎'}, ScriptMap{U'n', U'ⁿ', U'ₙ'},
    ScriptMap{U'i', U'ⁱ', U'ᵢ'}, ScriptMap{U'a', U'ᵃ', U'ₐ'},
    ScriptMap{U'e', U'ᵉ', U'ₑ'}, ScriptMap{U'o', U'ᵒ', U'ₒ'},
    ScriptMap{U'x', U'ˣ', U'ₓ'}, ScriptMap{U'h', U'ʰ', U'ₕ'},
    ScriptMap{U'k', U'ᵏ', U'ₖ'}, ScriptMap{U'l', U'ˡ', U'ₗ'},
    ScriptMap{U'm', U'ᵐ', U'ₘ'}, ScriptMap{U'p', U'ᵖ', U'ₚ'},
    ScriptMap{U's', U'ˢ', U'ₛ'}, ScriptMap{U't', U'ᵗ', U'ₜ'},
    ScriptMap{U'r', U'ʳ', U'ᵣ'}, ScriptMap{U'u', U'ᵘ', U'ᵤ'},
    ScriptMap{U'v', U'ᵛ', U'ᵥ'}, ScriptMap{U'd', U'ᵈ', U'd'},
    ScriptMap{U'g', U'ᵍ', U'g'},      ScriptMap{U'b', U'ᵇ', U'b'},
    ScriptMap{U'c', U'ᶜ', U'c'},      ScriptMap{U'f', U'ᶠ', U'f'},
    ScriptMap{U'j', U'ʲ', U'j'},      ScriptMap{U'w', U'ʷ', U'w'},
    ScriptMap{U'y', U'ʸ', U'y'},      ScriptMap{U'z', U'ᶻ', U'z'},
};

std::string map_script(std::string_view s, bool superscript_form) {
  std::string out;
  out.reserve(s.size() * 2);
  for (std::size_t i = 0; i < s.size();) {
    const auto [cp, n] = decode_utf8(s, i);
    i += n;
    const auto it = std::ranges::find(kScripts, cp, &ScriptMap::plain);
    append_utf8(out, it == kScripts.end() ? cp : (superscript_form ? it->sup : it->sub));
  }
  return out;
}

/// Length of the ANSI escape sequence starting at `pos` (0 if none).
std::size_t escape_length(std::string_view s, std::size_t pos) noexcept {
  if (pos >= s.size() || s[pos] != '\x1b') return 0;
  std::size_t j = pos + 1;
  if (j < s.size() && s[j] == '[') {
    ++j;
    while (j < s.size() && !(s[j] >= 0x40 && s[j] <= 0x7E)) ++j;
    return (j < s.size() ? j + 1 : j) - pos;
  }
  if (j < s.size() && s[j] == ']') {  // OSC ... BEL / ST
    ++j;
    while (j < s.size() && s[j] != '\x07' && !(s[j] == '\x1b' && j + 1 < s.size() && s[j + 1] == '\\')) ++j;
    if (j < s.size()) j += s[j] == '\x07' ? 1 : 2;
    return j - pos;
  }
  return j < s.size() ? 2 : 1;
}

struct Atom {
  std::string_view bytes;
  int width{0};
  bool escape{false};
  bool space{false};
  bool wide{false};
};

std::vector<Atom> atomize(std::string_view s) {
  std::vector<Atom> atoms;
  atoms.reserve(s.size());
  for (std::size_t i = 0; i < s.size();) {
    if (const std::size_t n = escape_length(s, i); n > 0) {
      atoms.push_back(Atom{.bytes = s.substr(i, n), .escape = true});
      i += n;
      continue;
    }
    const auto [cp, n] = decode_utf8(s, i);
    const bool space = cp == U' ' || cp == U'\t';
    const int width = space ? 1 : column_width(cp);
    atoms.push_back(Atom{.bytes = s.substr(i, n), .width = width, .space = space, .wide = width == 2});
    i += n;
  }
  return atoms;
}

}  // namespace

int column_width(char32_t cp) noexcept {
  if (cp == 0 || cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) return 0;
  if (in_ranges(cp, kZeroWidth)) return 0;
  if (in_ranges(cp, kWide)) return 2;
  return 1;
}

std::size_t display_width(std::string_view s) noexcept {
  std::size_t width = 0;
  for (std::size_t i = 0; i < s.size();) {
    if (const std::size_t n = escape_length(s, i); n > 0) {
      i += n;
      continue;
    }
    const auto [cp, n] = decode_utf8(s, i);
    width += static_cast<std::size_t>(column_width(cp));
    i += n;
  }
  return width;
}

std::string strip_ansi(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size();) {
    if (const std::size_t n = escape_length(s, i); n > 0) {
      i += n;
      continue;
    }
    out += s[i++];
  }
  return out;
}

std::vector<std::string> wrap(std::string_view text, std::size_t width, std::string_view first_indent,
                              std::string_view rest_indent) {
  std::vector<std::string> lines;
  std::string active;  // SGR state carried across line breaks
  bool first_line = true;

  std::size_t start = 0;
  while (true) {
    const std::size_t newline = text.find('\n', start);
    const std::string_view segment =
        text.substr(start, newline == std::string_view::npos ? std::string_view::npos : newline - start);

    const std::string_view indent = first_line ? first_indent : rest_indent;
    const auto atoms = atomize(segment);

    if (atoms.empty()) {
      lines.emplace_back();
      first_line = false;
    } else {
      std::string current{indent};
      current += active;
      std::size_t columns = display_width(indent);
      bool has_text = false;
      bool pending_space = false;

      const auto break_line = [&] {
        if (!active.empty()) current += sgr_reset;
        lines.push_back(std::move(current));
        first_line = false;
        current = std::string(rest_indent) + active;
        columns = display_width(rest_indent);
        has_text = false;
        pending_space = false;
      };

      const std::size_t limit = std::max(width, columns + 1);

      for (std::size_t i = 0; i < atoms.size();) {
        if (atoms[i].space) {
          pending_space = has_text;
          ++i;
          continue;
        }
        // A chunk is a run that must not be split: a word, or a single wide
        // character (CJK text breaks between any two characters).
        std::size_t j = i;
        std::size_t chunk_width = 0;
        while (j < atoms.size() && !atoms[j].space) {
          if (j > i && !atoms[j].escape && (atoms[j].wide || atoms[j - 1].wide)) break;
          chunk_width += static_cast<std::size_t>(atoms[j].width);
          ++j;
        }

        const std::size_t needed = chunk_width + (pending_space ? 1 : 0);
        if (has_text && columns + needed > limit) {
          break_line();
        } else if (pending_space) {
          current += ' ';
          ++columns;
          pending_space = false;
        }

        for (std::size_t k = i; k < j; ++k) {
          const Atom& atom = atoms[k];
          if (atom.escape) {
            current += atom.bytes;
            active = is_sgr_reset(atom.bytes) ? std::string{} : std::string(atom.bytes);
            continue;
          }
          if (has_text && columns + static_cast<std::size_t>(atom.width) > limit) {
            break_line();  // a single word longer than the line: hard split
          }
          current += atom.bytes;
          columns += static_cast<std::size_t>(atom.width);
          has_text = true;
        }
        i = j;
      }

      if (!active.empty()) current += sgr_reset;
      lines.push_back(std::move(current));
      first_line = false;
    }

    if (newline == std::string_view::npos) break;
    start = newline + 1;
  }
  return lines;
}

std::string truncate(std::string_view s, std::size_t width, std::string_view ellipsis) {
  if (display_width(s) <= width) return std::string(s);
  const std::size_t ellipsis_width = display_width(ellipsis);
  const std::size_t budget = width > ellipsis_width ? width - ellipsis_width : 0;
  std::string out;
  std::size_t used = 0;
  for (std::size_t i = 0; i < s.size();) {
    const auto [cp, n] = decode_utf8(s, i);
    const auto w = static_cast<std::size_t>(column_width(cp));
    if (used + w > budget) break;
    out.append(s.substr(i, n));
    used += w;
    i += n;
  }
  out += ellipsis;
  return out;
}

std::string_view trim(std::string_view s) noexcept {
  constexpr std::string_view blanks{" \t\r\n\f\v\0", 7};
  const std::size_t first = s.find_first_not_of(blanks);
  if (first == std::string_view::npos) return {};
  const std::size_t last = s.find_last_not_of(blanks);
  return s.substr(first, last - first + 1);
}

std::string to_lower_ascii(std::string_view s) {
  std::string out(s);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return out;
}

bool iequals_ascii(std::string_view a, std::string_view b) noexcept {
  return std::ranges::equal(a, b, [](char x, char y) {
    const auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
    return lower(x) == lower(y);
  });
}

std::string decode_entities(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size();) {
    if (s[i] != '&') {
      out += s[i++];
      continue;
    }
    const std::size_t end = s.find(';', i + 1);
    if (end == std::string_view::npos || end - i > 12) {
      out += s[i++];
      continue;
    }
    const std::string_view name = s.substr(i + 1, end - i - 1);
    std::optional<char32_t> cp;
    if (!name.empty() && name[0] == '#') {
      std::uint32_t value = 0;
      const bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
      const std::string_view digits = name.substr(hex ? 2 : 1);
      if (!digits.empty()) {
        const auto [ptr, ec] =
            std::from_chars(digits.data(), digits.data() + digits.size(), value, hex ? 16 : 10);
        if (ec == std::errc{} && ptr == digits.data() + digits.size() && value <= 0x10FFFF) {
          if (value >= 0x80 && value <= 0x9F) value = kCp1252[value - 0x80];
          cp = value == 0 ? U'�' : static_cast<char32_t>(value);
        }
      }
    } else if (const auto it = std::ranges::find(kEntities, name, &Entity::name); it != kEntities.end()) {
      cp = it->code_point;
    }
    if (!cp) {
      out += s[i++];
      continue;
    }
    append_utf8(out, *cp);
    i = end + 1;
  }
  return out;
}

std::string superscript(std::string_view s) { return map_script(s, true); }
std::string subscript(std::string_view s) { return map_script(s, false); }

std::string repeat(std::string_view piece, std::size_t count) {
  std::string out;
  out.reserve(piece.size() * count);
  for (std::size_t i = 0; i < count; ++i) out += piece;
  return out;
}

std::string pad_right(std::string_view s, std::size_t width) {
  std::string out(s);
  const std::size_t current = display_width(s);
  if (current < width) out.append(width - current, ' ');
  return out;
}

std::string with_thousands(std::uint64_t value) {
  std::string digits = std::to_string(value);
  std::string out;
  out.reserve(digits.size() + digits.size() / 3);
  for (std::size_t i = 0; i < digits.size(); ++i) {
    if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
    out += digits[i];
  }
  return out;
}

std::string human_size(std::uint64_t bytes) {
  constexpr std::array<std::string_view, 5> units{"B", "KiB", "MiB", "GiB", "TiB"};
  double value = static_cast<double>(bytes);
  std::size_t unit = 0;
  while (value >= 1024.0 && unit + 1 < units.size()) {
    value /= 1024.0;
    ++unit;
  }
  return unit == 0 ? std::format("{} B", bytes) : std::format("{:.1f} {}", value, units[unit]);
}

}  // namespace minedict::cli::text
