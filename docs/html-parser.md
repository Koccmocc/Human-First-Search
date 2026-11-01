# HTML parser decision

Selected dependency: **Lexbor 3.0.0**, commit
`2ae88a1c6b5261830eff73ee12bb3cdf805f3cfe`.

We need HTML parsing with DOM construction and error recovery for real web pages.
An XML parser would impose different rules; regular expressions cannot replace
HTML tree construction. Lexbor exposes a C API usable from C++ and provides a
CMake target with propagated include paths and static-library definitions.

- [Upstream release](https://github.com/lexbor/lexbor/releases/tag/v3.0.0)
- [HTML API](https://lexbor.com/modules/html/)
- [Pinned license](https://github.com/lexbor/lexbor/blob/2ae88a1c6b5261830eff73ee12bb3cdf805f3cfe/LICENSE)
- [Pinned NOTICE](https://github.com/lexbor/lexbor/blob/2ae88a1c6b5261830eff73ee12bb3cdf805f3cfe/NOTICE)

License: Apache-2.0. The downloaded source includes LICENSE and NOTICE; preserve
these notices when redistributing the dependency, including with binary packages.
We do not modify the upstream source. This choice does not set the project's own
license; packaging and complete third-party notices remain a later task.

`cmake/HtmlParser.cmake` downloads the pinned source with FetchContent. Git and
network access are needed on first configuration. Static linking avoids a separate
Lexbor DLL. Upstream examples and tests are disabled; our small C++ test checks the
API, allocation/destruction, omitted closing tags and entity decoding. It is a
compatibility smoke test, not an HTML-conformance or extraction-quality test.

Verified locally on 2026-11-01 with MinGW-w64 GCC/g++ 15.2.0, CMake 4.2.1 and
Ninja 1.13.2: Debug and Release builds and both CTest entries pass. Validation used
the exact pinned Git checkout through the source override below; FetchContent's
automatic network download was not exercised. No absolute path is committed.

For an already downloaded exact source revision, CMake supports an offline override:

```powershell
cmake --preset debug -DFETCHCONTENT_SOURCE_DIR_LEXBOR=C:/path/to/lexbor
```

The caller must verify the revision for such an override. Do not commit a local
absolute path. Omit the override for the ordinary pinned download.

The parser is now a private dependency of `crawler_core`, wrapped by
`hfs::HtmlDocument`. The public C++ header hides its C types; document ownership
and title extraction are tested separately from the direct C API smoke test.
Finding main article content,
excluding navigation/scripts, resolving links and handling source encodings are
separate tasks; successfully building a DOM does not solve them.
