# Human-first Search

Local university proof of concept. The C++ library currently provides HTML/URL
utilities and a bounded single-request HTTP client. The CLI only supports help
and version; automated crawling, main-article extraction and JSONL export are
not implemented yet. Development is independent of the team's Python/API/UI.

## Build and run

Run these commands from the repository root (`Search-Engine`):

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
./build/debug/crawler/hfs-crawler.exe --help
./build/debug/crawler/hfs-crawler.exe --version
```

Release uses the same sequence with the `release` preset. On Linux the executable
has no `.exe` suffix. CTest runs the executable for you on either platform.

Requirements: CMake 3.30+, Ninja, and GCC with C++20 support.
The initial environment uses MinGW-w64 GCC 15.2.0,
CMake 4.2.1 and Ninja 1.13.2. These tools must be on `PATH`; on Windows the GCC
`bin` directory must also remain on `PATH` when running executables so their
runtime DLLs can be found. Do not mix different MinGW installations.

Presets select `g++` from `PATH`, without machine-specific absolute paths.
All project targets use standard C++20 without GNU language extensions.
Git and network access are needed on first configuration to download the pinned
Lexbor HTML parser. It is built from C with `gcc`; the crawler uses `g++`.
See [the parser decision](docs/html-parser.md) for the version, license and offline override.
With `BUILD_TESTING=ON` (the preset default), configuration also downloads the
pinned GoogleTest 1.18.0 source. It is not needed by the application itself.
Python 3 is required for tests: its standard library hosts the loopback HTTP fixture.
Configuration also downloads the pinned libcurl source described below.
The robots.txt parser and its Abseil dependency are also fetched at pinned revisions.

## Structure and decisions

### VS Code IntelliSense

Open `Search-Engine` as the workspace folder. The committed `.vscode/settings.json`
selects C++20 and reads compiler flags, defines and include paths from
`build/debug/compile_commands.json`. Run `cmake --preset debug` to generate it.
The compiler and public include directory are also specified as fallbacks for
headers that are not yet associated with a translation unit. System include paths
are discovered from `g++`; do not copy standard library headers into the project.

After changing editor settings, run **C/C++: Reset IntelliSense Database** from
the Command Palette. If diagnostics remain stale, run **Developer: Reload Window**.
Use **C/C++: Log Diagnostics** to check the selected compiler and language standard.
When opening a parent folder instead, its workspace settings must point to the
nested project's compilation database; nested `.vscode` settings are not applied.

### Source layout

- `CMakeLists.txt`: project version, required language standard and CTest setup.
- `CMakePresets.json`: separate Debug/Release directories and Ninja/GCC selection.
- `crawler/include/hfs/`: public interfaces of the crawler library.
- `crawler/src/cli.cpp`: argument handling, independent of console output.
- `crawler/src/main.cpp`: process entry point, output and exit codes.
- `crawler/tests/cli_test.cmake`: CLI checks against the built executable.
- `docs/liubomyr-plan.md`: scope, implementation stages and draft corpus contract.

`crawler_core` is a static library so future URL, extraction and export logic can
be tested without going through `main`. `CliResult` uses `std::variant` to hold
either a `CliAction` or a `CliError`, keeping success and failure mutually exclusive
without exceptions for ordinary argument errors. Console output uses standard
streams, requiring no additional terminal-support library.
Exit code `0` means success and `2` means invalid arguments;
no arguments display help. Diagnostics go to stderr, successful output to stdout.
Unsupported arguments fail rather than starting an unintended operation.

Warnings are enabled on our targets only. They are not treated as errors yet:
diagnostic changes between GCC versions should not prevent a fresh build.
CTest is available in both Debug and Release; checks do not rely on `assert`,
which is normally disabled in Release. `compile_commands.json` is generated in
each build directory for editor tooling. Local overrides belong in the ignored
`CMakeUserPresets.json`; generated builds and corpus data are also ignored.

`hfs::HtmlDocument` now owns a Lexbor DOM inside `crawler_core`. Its public header
does not expose Lexbor types. Construct it from UTF-8 HTML and call `title()` for
an owning string with decoded entities and normalized title whitespace. Missing
titles return an empty string. The input buffer may be released after construction.
Lexbor failures throw `std::runtime_error`; C++ allocation may throw `std::bad_alloc`.
Malformed markup is handled by the HTML parser and is not automatically a failure.

Documents cannot be copied, but can be moved. A moved-from document has an empty
title and can be destroyed or assigned to. An opaque `Impl` keeps the C API private;
its `unique_ptr` releases the DOM on normal exit and on construction failure.
Tests cover input/title lifetimes, moves, absent titles, whitespace and entities.

### Unit tests

`cmake/Testing.cmake` pins GoogleTest 1.18.0 to commit
`063de7e9578f82b369302001269680b4b1553359`. Its
[BSD-3-Clause license](https://github.com/google/googletest/blob/063de7e9578f82b369302001269680b4b1553359/LICENSE)
must be preserved when redistributing the dependency. GoogleMock is disabled.
GoogleTest is downloaded and configured only when `BUILD_TESTING` is enabled.
For offline configuration, supply `FETCHCONTENT_SOURCE_DIR_GOOGLETEST` pointing
to a verified checkout of that revision, as with the Lexbor source override.

`TEST(HtmlDocument, CaseName)` defines one independent scenario. `EXPECT_EQ` and
`EXPECT_TRUE` report failures without stopping the scenario; use `ASSERT_*` when
continuing after a failed prerequisite would be invalid. `GTest::gtest_main`
provides the test runner, so these tests need no custom `main` or failure counter.
`gtest_discover_tests` registers each case with CTest; both Debug and Release run
the same checks. The direct Lexbor smoke test and CLI process test are unchanged.

```powershell
ctest --preset debug -R HtmlDocument
```

### Body text

`HtmlDocument::body_text()` returns an owning UTF-8 string from the body DOM.
It skips comments and entire `script`, `style` and `template` subtrees. Text nodes
already contain decoded HTML entities. HTML ASCII whitespace and UTF-8 NBSP are
collapsed; a fixed set of block elements, table cells and `br` introduce one
newline. Leading/trailing separators are omitted. Inline markup inserts no space,
so `Hel<b>lo</b>` remains `Hello`. Even `pre` whitespace is normalized for this
search-oriented representation. Other Unicode spacing characters are preserved.

Traversal follows DOM parent/sibling links without recursion or DOM mutation.
Empty and moved-from documents return an empty string. Tests cover formatting,
excluded subtrees, UTF-8 entities, result ownership, moves and deep nesting.

This is structural body text, not article selection or browser-visible text:
navigation, footers and hidden elements may remain. CSS is not evaluated and no
JavaScript is executed. These limitations must be considered before corpus export.
The CLI still only supports help and version. Article selection, local file input
and JSONL export will follow in separate changes.

### Link candidates

`HtmlDocument::links()` returns owning strings from `href` on HTML `a` and `area`
elements, in DOM order. Missing attributes are skipped; empty and valueless
attributes produce empty strings. Duplicates and attribute whitespace are retained.
HTML entities are decoded by Lexbor. Templates and foreign elements such as SVG
anchors are excluded; resource elements such as `link` and `img` are not crawled.
Moved-from documents return an empty vector.

These are raw link candidates, not validated crawl targets. Relative references,
fragments and non-HTTP schemes remain unchanged. The method does not apply `base`,
resolve URLs, deduplicate, check robots.txt or enforce allowed domains. A URL layer
must do that before any link enters the crawl queue. No network requests occur.

### URL resolution

`hfs::resolve_http_url(base, reference)` uses Lexbor's URL parser, returning an
owning absolute HTTP(S) URL or `std::nullopt` when parsing fails or the scheme or
credentials are rejected. The base must itself be an absolute HTTP(S) URL without
credentials, even when the reference is absolute. Parser initialization and
serialization failures throw; C++ allocation failures can also propagate.

The serializer removes fragments, normalizes scheme/host and default ports, and
resolves dot segments. It preserves path case, query parameter order and repeated
parameters. Empty and fragment-only references resolve to the current page, so a
future visited-URL set must handle them. This is a stable parser serialization,
not a guarantee that different URLs identify different or identical resources.

This function does not check allowed hosts, robots.txt, resolve DNS, apply HTML
`base` elements or make requests. Callers still need crawl-scope validation.
Tests live in `crawler/tests/url_test.cpp`; run them with:

```powershell
ctest --preset debug -R crawler.Url
```

### Allowed hosts

`http_url_host(url)` extracts a canonical host from an absolute, credential-free
HTTP(S) URL. It returns no port: `https://EXAMPLE.org:8443/path` yields `example.org`.
IDNs are serialized as ASCII, IPv6 literals retain brackets. Invalid URLs and
unsupported schemes return `std::nullopt`; initialization/serialization failures
can throw, as with URL resolution.

`is_allowed_host(url, allowed_hosts)` compares that parsed host to an explicit
list of canonical host strings. Prepare entries with `http_url_host` rather than
passing full URLs, wildcards or unnormalized names. Empty lists deny all hosts.
Subdomains must be listed separately; `example.org.evil.com` and `example.org.`
do not match `example.org`. Path, query and user-info cannot supply a match.

This is a host-only policy: both HTTP and HTTPS and all ports of an allowed host
are accepted. It does not inspect DNS/IP destinations, robots.txt or redirects.
The crawler will need to apply scope checks before each request, including every
redirect target. No requests or queue integration are implemented in this step.

### HTTP transport dependency

`cmake/HttpTransport.cmake` pins curl/libcurl 8.23.0 to commit
`3163ec065643ba20709f190111f42c3e1fe95273` and builds a static library from source.
The command-line curl tool, upstream tests and manuals are disabled. Generic
CMake options are scoped inside a function so curl does not disable our tests.
Optional compression, HTTP/2 and third-party SSH/PSL/IDN dependencies are disabled
for the initial build. Additional protocols may remain compiled in; the HTTP
client explicitly restricts transfers to HTTP(S) and disables automatic redirects.

On Windows TLS uses Schannel and the system certificate store. On other platforms
the configuration selects OpenSSL, requiring matching development headers and
libraries; that path has not been tested here. The client explicitly enables
certificate and hostname verification; a live TLS connection has not yet been tested.

The dependency uses the [curl license](https://github.com/curl/curl/blob/3163ec065643ba20709f190111f42c3e1fe95273/COPYING).
Preserve its copyright and permission notice when distributing copies.
For a verified local checkout, configure with `FETCHCONTENT_SOURCE_DIR_CURL`.
Initial verification uses that override on MinGW-w64 GCC 15.2.0; automatic
FetchContent network retrieval has not yet been exercised.

`crawler/tests/curl_test.cpp` checks global initialization, handle creation,
HTTP/HTTPS capabilities and the Windows TLS backend without network requests.
libcurl is also a private dependency of `crawler_core` for the HTTP client below.

### Single-request HTTP client

`http_get(url, options)` performs one synchronous GET. Defaults are a 3-second
connection timeout, 10-second total timeout and a 2 MiB body limit. Options must
be positive. URLs pass through the HTTP(S) resolver, which rejects credentials
and removes fragments. Redirects and retries are disabled. Proxy environment
variables are intentionally ignored for this initial direct-connection transport.
The User-Agent is `HumanFirstSearch/<project version>`.

`HttpResult` holds either `HttpResponse` (status, body bytes, Content-Type and
optional redirect Location) or
`HttpError` (invalid request, timeout, body limit or transport failure). HTTP
4xx/5xx are responses, not transport failures. Interrupted/oversized transfers
return an error without exposing a partial body as a complete response. The body
limit is checked before appending, including when Content-Length is absent.
It does not bound all libcurl allocations or response headers. Body bytes are
not decompressed or converted to UTF-8 here; HTML/type/encoding policy is future work.

libcurl global initialization is shared inside the module; each call owns and
releases its easy handle. Configuration/initialization errors and C++ allocation
failures can throw; callback exceptions are captured and rethrown after libcurl
returns, never unwound through C frames.

This is a transport primitive: the caller must enforce host scope, robots.txt
and per-host delays before requesting a URL. It is not yet exposed through the CLI.
`HttpResponse::redirect_location` copies the first Location header from
301/302/303/307/308 responses, using libcurl's response-header API. It is absent
when no header is present or the status is not one of these redirects. The value
is not a validated absolute URL: resolve it against the requested URL with
`resolve_http_url`, then check host scope and robots.txt before another request.
Unsupported schemes and credentials remain visible for validation instead of
being silently accepted. `http_get` itself still performs exactly one request;
the optional redirect layer below performs controlled transitions.

`crawler/tests/http_fixture.py` starts a loopback server on an OS-selected port,
runs seventeen GoogleTest scenarios and shuts down the server. Tests cover normal and
empty responses, 404/302, an unfollowed redirect, unknown-length body limits, a
timeout, a truncated response, invalid arguments and redirect metadata across
301/302/303/307/308. They also cover missing Location and non-redirect statuses.
They require no internet or
third-party Python packages; local socket access is required.

### Controlled redirect fetching

`fetch_http(url, permission, options, max_redirects)` wraps `http_get`. Supply a
`RequestPermission` callback accepting a canonical absolute URL and returning
whether a request is allowed. It runs before every request, including the initial
one; an empty callback denies all. For now the caller can use `is_allowed_host`.
Robots retrieval and per-host scheduling have not yet been implemented. The
`robots_allows` matcher below can evaluate already obtained rules; combine these
checks in the request policy before live crawling. Policy exceptions propagate.

The default is at most five transitions (six requests). Zero allows only the
initial request. Repeated canonical URLs, invalid targets and denied requests stop
the chain before another request. URL normalization removes fragments, so a
fragment-only cycle is detected too. HTTP responses without redirect metadata,
including 4xx/5xx or a 302 without Location, terminate the chain normally.

`FetchResult` contains a `FetchResponse` (final URL, response and ordered requested
URLs), the original `HttpError`, or a `FetchError` describing policy denial, invalid
URL, a redirect cycle or the hop limit. Failed chains do not expose a successful
partial result. Timeout/body limits apply to each request independently, not to the
whole chain. There are no retries or HTTPS-downgrade restrictions yet. The CLI is
unchanged. Tests exercise an allowed chain, denied initial/redirect requests,
cross-host denial, transition limits, cycles and transport-error propagation.

### Robots.txt parser dependency

`cmake/RobotsParser.cmake` builds Google's robots.txt parser/matcher at commit
`22b355ff855419e6a3ff8ff09c0ad7fdb17116f9` with Abseil 20250814.2 at commit
`0cf0a5c9d12cc3783363ab20f11613e69fd04c9a`. Both use Apache-2.0:
[parser license](https://github.com/google/robotstxt/blob/22b355ff855419e6a3ff8ff09c0ad7fdb17116f9/LICENSE),
[Abseil license](https://github.com/abseil/abseil-cpp/blob/0cf0a5c9d12cc3783363ab20f11613e69fd04c9a/LICENSE).
Preserve license/copyright notices in distributed copies and document modifications
if upstream files are changed. Our integration builds unmodified source.

Upstream parser CMake bootstraps dependencies through ExternalProject, so we skip
that build script via FetchContent's SOURCE_SUBDIR and define a small static target
using `robots.cc`, `absl::base`, `absl::strings` and `absl::fixed_array`. This keeps
dependency revisions explicit and avoids upstream executables/install rules.

Verified locally with MinGW GCC 15.2.0 using exact checkouts through
`FETCHCONTENT_SOURCE_DIR_ROBOTSTXT` and `FETCHCONTENT_SOURCE_DIR_ABSEIL` overrides.
Automatic FetchContent downloads have not been exercised. Abseil emits MinGW
warnings for MSVC pragmas, a type-range comparison and a `%zu` diagnostic in its
Base64 error path. They remain visible; our smoke tests do not establish correctness
of all Abseil functionality or full robots-protocol conformance.

`robots_parser_test.cpp` covers empty rules, Disallow, more-specific Allow, agent
selection, wildcard/end patterns, comments and query matching. The matcher expects
normalized URLs; normalization remains our URL module's responsibility.
The dependency is now used privately by `crawler_core` through `robots_allows`.
It does not fetch/cache robots files, decide HTTP-failure policy or implement
per-host delays. A successfully retrieved empty file permits access under matching
rules; a network error must not be represented as an empty file. Fetch/cache policy
and integration into `RequestPermission` are separate increments.

```powershell
ctest --preset debug -R crawler.RobotsParser --output-on-failure
```

### Matching robots rules through our interface

`hfs::robots_allows(robots_txt, url)` evaluates known robots.txt text for the
`HumanFirstSearch` product token. `crawler_identity.hpp` defines this token once
for both matching and the HTTP User-Agent (`HumanFirstSearch/<version>`).
The wrapper normalizes an absolute HTTP(S) URL with our URL module first, removing
fragments and resolving dot segments. Invalid URLs and credentials are denied.
Only standard C++ types appear in the public header; Google and Abseil stay private.

Each call creates its own Google matcher and reparses the supplied text; no shared
matcher state or parsed-rule cache is maintained. Allocation/normalization errors
can throw. Empty rules permit valid URLs, so callers must supply text only for a
known rules state and must never convert a network failure into an empty document.
The wrapper does not fetch anything or associate text with an origin: callers
must select the correct rules for the URL's scheme, host and port. It does not
check our host allowlist, interpret HTTP statuses or implement Crawl-delay.

`robots_test.cpp` covers the product group, URL normalization including percent
encoding, invalid inputs, empty rules and independence between calls. The local
HTTP fixture verifies that requests send the shared product token. Robots loading,
cache/failure states and automatic policy integration remain future increments.

### Locating robots.txt

`robots_txt_url(page_url)` returns the canonical HTTP(S) address of `/robots.txt`
at the page's scheme, host and port, following the location rule in
[RFC 9309 section 2.3](https://www.rfc-editor.org/rfc/rfc9309.html#section-2.3).
It uses the existing URL resolver to replace the page path, query and fragment.
For example, `https://example.org:8443/dir/page?x=1` produces
`https://example.org:8443/robots.txt`.

The result can identify a future cache entry: HTTP and HTTPS and non-default
ports stay separate, while equivalent default-port spellings share one key.
This differs from the host-only allowlist, which intentionally ignores ports.
Invalid/relative URLs, unsupported schemes and credentials return `std::nullopt`.
The helper performs no request and does not decide access. Tests cover origin
separation, canonical spellings, IPv6/IDN and invalid inputs. Downloading and
caching rules remain the next increment.
