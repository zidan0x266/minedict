# minedict

A terminal dictionary for MDX/MDD files, written in C++23.

`minedict` opens the dictionaries made for MDict (the `.mdx` word files and
their `.mdd` resource files), indexes every headword, and renders entries in
the terminal with the [Dracula](https://draculatheme.com) palette: block
layout, lists, tables, super/subscripts, redirects, and the dictionary's own
CSS. It ships as a small library (`minedict::core`, the reader and index) and
a front end (`minedict::cli`) built on top of it.

```
$ minedict -d examples cake
cake                                              Toy English-Chinese Dictionary
────────────────────────────────────────────────────────────────────────────────
  cake /keik/ ★★★ 中考 高考 CET-4 考研

  释义
  1. n. 蛋糕, 块, 饼
  2. vt. 使结块, 加块状物于
  3. vi. 结块

  English
  ⇒ n. a block of solid substance (such as soap or wax)
  ⇒ n. baked goods made from or based on a mixture of flour, sugar, eggs, and
  fat

  词形 复数: cakes 过去分词: caked 现在分词: caking 过去式: caked
```

## Building

You need CMake 3.24+ and a C++23 toolchain: GCC 13+, Clang 17+ with libc++ 17+,
or Xcode 15.3+. Optional: GNU readline or libedit for the interactive mode,
and iconv (present on macOS and glibc systems) to decode Big5/GBK dictionaries.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build          # the unit tests
./build/bin/minedict -d examples cake
```

CMake options: `-DMINEDICT_BUILD_CLI=OFF` (library only), `-DMINEDICT_BUILD_TESTS=OFF`,
`-DMINEDICT_WITH_READLINE=OFF`, `-DMINEDICT_WITH_ICONV=OFF`,
`-DMINEDICT_WARNINGS_AS_ERRORS=ON`. `cmake --install build` installs the
executable, the core library, its headers and a CMake package
(`find_package(minedict)` then link `minedict::core`).

## Using the dictionary

```bash
minedict -d oxford.mdx serendipity          # render an entry
minedict -d oxford.mdx -d ~/dicts word      # several dictionaries, or a folder of .mdx (+ .mdd) files
minedict oxford.mdx CAKE                    # bare .mdx/.mdd arguments work; keys are case-insensitive
minedict -d oxford.mdx --raw cake           # the stored HTML, for scripts
minedict -d oxford.mdx search -n 20 seren   # headwords starting with "seren"
minedict -d oxford.mdx search -c cake       # headwords containing "cake"
minedict -d oxford.mdx info                 # title, engine, encoding, blocks, entry count, CSS
minedict -d oxford.mdx list | wc -l         # every headword, one per line
minedict -d sounds.mdd extract '\word.mp3' -o word.mp3
printf 'cake\nbun\n' | minedict -d oxford.mdx   # batch mode: one word per line on stdin
```

Run `minedict -d oxford.mdx` with no word for the interactive mode: type a word,
press Tab to complete headwords from the index, and use slash commands:

```
/search <prefix>, /s     headwords by prefix          /limit <n>          results per search
/contains <text>, /c     headwords containing text    /width <n>|auto     wrap width
<number>                 open a result from the list  /theme dracula|plain
/raw <word>              the stored HTML              /links /images /pager /css /sounds
/dicts, /info [n]        the loaded dictionaries      /quit, /q           leave (or Ctrl-D)
```

History lives in `~/.local/state/minedict/history`. Without `-d`, dictionaries
come from `MINEDICT_DICTS` (colon-separated paths) and
`~/.config/minedict/dictionaries` (one path per line, `#` comments). Exit codes:
`0` found, `1` nothing found, `2` usage error, `3` no dictionary could be opened.

### Rendering

Entries are rendered from the dictionary's HTML: block elements become lines,
lists get bullets, tables become rows, `<sup>`/`<sub>` use Unicode scripts,
`@@@LINK=` redirects are followed (shown as `↳ via …`), the header's
`StyleSheet` markers are expanded, images become `[img: name]` and `sound://`
links are hidden unless `--sounds` marks them with ♪. Every colour the
dictionary asks for is remapped onto the Dracula accents so entries from
different dictionaries look alike; `--theme plain` keeps only bold/italic,
`--color never` (or `NO_COLOR`) disables styling.

A `.css` file next to a dictionary (`<name>.css`, or any `.css` in the folder)
is applied too. The subset a terminal can honour is: `display` (block, inline,
inline-block, none), `visibility`, `color`, `font-weight`, `font-style`,
`text-decoration`, `white-space`, `margin`/`padding` (as blank lines and
indent), `::before`/`::after` content, and CSS counters. That is what makes
dictionaries built from custom tags (OALD, LDOCE, …) readable, and it is how
[`examples/theme.css`](examples/theme.css) gives the toy dictionary its layout:
numbered senses, labelled sections and inflected forms all come from a few
class rules. `--no-css` turns stylesheets off.

## The example folder

`examples/` holds `toy.mdx`, a small English–Chinese dictionary of about 3,500
core words (Chinese and English glosses, pronunciation, inflections, exam
tags) written as an engine 2.0 file, and `theme.css`, its terminal stylesheet.
The entries are built from [ECDICT](https://github.com/skywind3000/ECDICT)
(MIT License). The tests use the same file, so the suite covers a real
dictionary as well as the engine 1.x and 2.0 layouts it builds in memory.

## Using the library

```cpp
#include <minedict/archive.hpp>
#include <minedict/index.hpp>

auto archive = minedict::Archive::open("oxford.mdx");      // Result<Archive>: std::expected
if (!archive) return report(archive.error().message);
minedict::Index index{archive->keys(), {.case_sensitive = false, .strip_punctuation = true}};
for (std::uint32_t position : index.exact("serendipity")) {
  std::string html = archive->text(position).value();     // UTF-8, whatever the file's encoding
  html = archive->header().stylesheet.apply(html);
}
```

`Archive` memory-maps the file, decodes every key when opened, and decodes
record blocks on demand (verifying their checksums). `Index` answers exact,
prefix, substring and fuzzy queries. Everything that can fail returns
`std::expected`; nothing throws across the API.

## Code layout

```
CMakeLists.txt          the project (cmake -S . -B build)
examples/               toy.mdx and theme.css
src/                    the program
├── include/minedict/       core headers (namespace minedict)
│   ├── archive.hpp         one MDX/MDD file: header, keys, lazy records
│   ├── index.hpp           exact / prefix / substring / fuzzy search
│   ├── header.hpp          the UTF-16 XML header, StyleSheet, versions
│   ├── compression.hpp     block envelope: tag, Adler-32, zlib / LZO / stored
│   ├── inflate.hpp         DEFLATE / zlib decoder
│   ├── lzo.hpp             LZO1X decoder
│   ├── encoding.hpp        UTF-8, UTF-16LE natively; Big5/GBK through iconv
│   ├── unicode.hpp         code point utilities and case folding
│   ├── keys.hpp            headword normalisation, edit distance
│   ├── ripemd128.hpp       the hash behind the encrypted key index
│   ├── bytes.hpp           byte views and a bounds-checked big-endian reader
│   └── error.hpp           Error, Result<T> = std::expected<T, Error>
├── include/minedict/cli/   front-end headers (namespace minedict::cli)
│   ├── dictionary, shelf   a dictionary with its index and CSS; many of them
│   ├── html, css, render   tokenizer, CSS subset, HTML → styled terminal text
│   ├── ansi, theme, text   colours, the Dracula palette, wrapping and width
│   └── options, terminal, presenter, repl, app
├── core/                   core implementation
└── cli/                    front-end implementation and main.cpp
tests/                  unit tests (check.hpp) and an in-memory MDX/MDD builder; run with ctest
```

## The MDX format, briefly

An MDX/MDD file is a UTF-16LE XML header (with an Adler-32), a key section
and a record section. Engine 1.x writes 4-byte integers and LZO blocks;
engine 2.0 writes 8-byte integers, zlib blocks, and may scramble the key index
with a RIPEMD-128 derived key (`Encrypted="2"`). Keys are NUL-terminated
strings paired with offsets into the concatenated record data; records are
sliced out of their block at those offsets. Dictionaries locked to a user's
registration code (`Encrypted="1"`) are not supported. See
[pyglossary's description](https://github.com/ilius/pyglossary/blob/master/doc/mdx.md)
for more detail.

## License

minedict is released under the BSD 3-Clause License.

```
Copyright (c) 2026 the minedict authors. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

The example dictionary is built from [ECDICT](https://github.com/skywind3000/ECDICT),
Copyright (c) Linwei, MIT License.
