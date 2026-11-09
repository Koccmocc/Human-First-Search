# Human-first Search

Local university proof of concept. The C++ crawler supports bounded traversal
from explicit seeds and allowed hosts, with robots.txt, request delays and UTF-8
HTML validation. The CLI prints discovered page URLs and session statistics,
and can export draft JSONL records. Main-article selection, language detection
and text deduplication remain future steps.
Development is independent of the team's Python/API/UI.

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
JSONL export additionally uses the pinned nlohmann/json header library.

## Run a bounded crawl

Example of a small live crawl, subject to the site's robots rules:

```powershell
./build/debug/crawler/hfs-crawler.exe crawl --seed https://example.org/ --allow-host example.org --max-pages 5 --max-targets 20 --max-depth 1
```

`--seed URL` and `--allow-host HOST` are both required and repeatable. Seeds must
be absolute HTTP(S) URLs without credentials. Hosts contain no scheme, path or
port; bracketed IPv6 is accepted. Host names and seeds are canonicalized using
the existing URL module. Repeated host names are collapsed. Subdomains require
their own explicit allowance. Every seed is checked against the complete host
list before crawling, regardless of argument order. An out-of-scope seed rejects
the entire command before any request occurs.

| Option | Default | Meaning |
| --- | --- | --- |
| `--max-pages N` | 1000 | Maximum delivered HTML records |
| `--max-targets N` | 5000 | Maximum popped queue entries, including failures |
| `--max-depth N` | 3 | Maximum link depth; seeds have depth zero |
| `--max-pending N` | 1000 | Pending queue capacity |
| `--max-urls N` | 5000 | Total admitted unique URL budget |
| `--delay-ms N` | 1000 | Per-host interval, shared by robots and page requests |

Pass options and values as separate arguments (`--max-pages 5`). Numbers must
be unsigned decimal integers in range. Only depth and delay may be zero; use
zero delay for local fixtures. Repeated numeric options, unknown arguments,
missing values and malformed inputs are errors. The CLI retains the library's
default HTTP/robots options and redirect limit of five; those are not CLI flags
yet. `--help`, `-h`, `--version`, and `crawl --help` are also available. No arguments
display help.

Stdout contains one final URL per delivered HTML page. With `--output PATH`, the
same callback also writes each page as a JSONL record. The parent directory must
exist; destinations already present at the preflight check are rejected. Stderr
contains a single session summary such as:

```text
crawl: stop=queue_empty pages=3 targets=4 fetch_failures=1 skipped_responses=0 duplicate_pages=0 rejected_seeds=0 pending_urls=0
```

Exit `0` means the session completed normally, including a configured limit or
zero delivered pages. Inspect the summary for failures and skipped responses;
exit `0` does not promise that a desired corpus size was reached. Invalid arguments
exit `2` before crawling. Fatal runtime or output errors exit `1`; pages printed
before such an error remain partial output. Help/version use stdout and argument
or runtime diagnostics use stderr.

Argument unit tests cover canonicalization, ownership, numeric boundaries and
invalid inputs. `crawler.cli_crawl` uses the real executable against the local
fixture to verify discovery, limits, validation before requests and failed-page
statistics. It needs no internet.

## Export JSONL records

Choose a new output file and run from the repository root:

```powershell
New-Item -ItemType Directory -Force data | Out-Null
./build/debug/crawler/hfs-crawler.exe crawl --seed https://example.org/ --allow-host example.org --max-pages 5 --max-depth 1 --output data/corpus.jsonl
```

The file contains one compact UTF-8 JSON object per LF-terminated line, without
a BOM. Pages stream to disk as they arrive. An empty result creates an empty
file. The CLI checks open, write and close failures and exits `1` on fatal output
errors; a failed run can leave partial output. Stdout remains the URL list and
stderr remains the summary. See [the draft corpus format](docs/corpus-format.md)
for fields, unknown metadata and current extraction limits.

`cmake/Json.cmake` pins nlohmann/json 3.12.0 to commit
`55f93686c01528224f448c19128836e7df245f72`. Its
[MIT license](https://github.com/nlohmann/json/blob/55f93686c01528224f448c19128836e7df245f72/LICENSE.MIT)
must accompany redistributed library code. Upstream tests/install are disabled;
its headers remain private to serialization and its own tests. Offline configuration
can use `FETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON` pointing to a verified checkout.
The MinGW checks used this override; automatic FetchContent downloading remains
unverified. The library adds no runtime DLL.

JSONL unit tests cover schema fields, escaping, repeatability, invalid UTF-8 and
stream failures. The CLI fixture independently reads exports with Python `json`,
verifies IDs/text hashes with `hashlib`, checks real UTC capture times and stable
fields across repeated live exports, and covers multiple/empty outputs and failures
before HTTP requests. Fixed-record
unit tests still verify byte-for-byte serialization, including the captured time.

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
- `crawler/tests/cli_test.cmake`: help/version and argument-error process checks.
- `crawler/tests/cli_crawl_test.py`: local crawl checks against the built executable.
- `docs/liubomyr-plan.md`: scope, implementation stages and draft corpus contract.

`crawler_core` is a static library so future URL, extraction and export logic can
be tested without going through `main`. `CliResult` uses `std::variant` to hold
either a `CliAction`, an owning `CrawlCommand` or a `CliError`, keeping commands and errors mutually exclusive
without exceptions for ordinary argument errors. Console output uses standard
streams, requiring no additional terminal-support library.
Exit codes and stream usage are described in the CLI section above.
No arguments display help.
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
Article selection and local file input remain separate changes. Draft JSONL export
preserves this structural body text, as documented above.

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
not decompressed or converted to UTF-8 here; the crawl input gate supplies HTML/type/encoding checks.

libcurl global initialization is shared inside the module; each call owns and
releases its easy handle. Configuration/initialization errors and C++ allocation
failures can throw; callback exceptions are captured and rethrown after libcurl
returns, never unwound through C frames.

This is a transport primitive: the caller must enforce host scope, robots.txt
and per-host delays before requesting a URL. The CLI uses it through the crawl session.
`HttpResponse::redirect_location` copies the first Location header from
301/302/303/307/308 responses, using libcurl's response-header API. It is absent
when no header is present or the status is not one of these redirects. The value
is not a validated absolute URL: resolve it against the requested URL with
`resolve_http_url`, then check host scope and robots.txt before another request.
Unsupported schemes and credentials remain visible for validation instead of
being silently accepted. `http_get` itself still performs exactly one request;
the optional redirect layer below performs controlled transitions.

`crawler/tests/http_fixture.py` starts a loopback server on an OS-selected port,
runs thirty-two GoogleTest scenarios and shuts down the server. Tests cover normal and
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
`PageFetcher` combines host scope and cached robots decisions in the page request
policy. `load_robots` retrieves origin-specific rules and `robots_allows` evaluates
their text. A shared host timer spaces permitted request attempts. Policy exceptions propagate.

The default is at most five transitions (six requests). Zero allows only the
initial request. Repeated canonical URLs, invalid targets and denied requests stop
the chain before another request. URL normalization removes fragments, so a
fragment-only cycle is detected too. HTTP responses without redirect metadata,
including 4xx/5xx or a 302 without Location, terminate the chain normally.

`FetchResult` contains a `FetchResponse` (final URL, response and ordered requested
URLs), the original `HttpError`, or a `FetchError` describing policy denial, invalid
URL, a redirect cycle or the hop limit. Failed chains do not expose a successful
partial result. Timeout/body limits apply to each request independently, not to the
whole chain. There are no retries or HTTPS-downgrade restrictions yet.
Tests exercise an allowed chain, denied initial/redirect requests,
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
Robots retrieval and HTTP-result handling live in `load_robots`. Successfully
retrieved empty rules permit access under matching rules. Download failures produce
an explicit blocking state. `RobotsCache` retains these states between page checks.
`PageFetcher` shares per-host timing between robots retrieval and page requests.

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
HTTP fixture verifies that requests send the shared product token. `load_robots`
provides origin binding and download-result states used by `RobotsCache`.

### Locating robots.txt

`robots_txt_url(page_url)` returns the canonical HTTP(S) address of `/robots.txt`
at the page's scheme, host and port, following the location rule in
[RFC 9309 section 2.3](https://www.rfc-editor.org/rfc/rfc9309.html#section-2.3).
It uses the existing URL resolver to replace the page path, query and fragment.
For example, `https://example.org:8443/dir/page?x=1` produces
`https://example.org:8443/robots.txt`.

The result identifies a cache entry: HTTP and HTTPS and non-default
ports stay separate, while equivalent default-port spellings share one key.
This differs from the host-only allowlist, which intentionally ignores ports.
Invalid/relative URLs, unsupported schemes and credentials return `std::nullopt`.
The helper performs no request and does not decide access. Tests cover origin
separation, canonical spellings, IPv6/IDN and invalid inputs. The loader below uses
this address to retrieve rules for each origin.

### Loading robots rules

`load_robots(page_url, bootstrap_permission, options)` obtains the origin's
robots.txt through `fetch_http`, with up to five controlled redirects. The
permission callback checks the configured network scope before each robots request.
Use a bootstrap permission callback independent of the rules being retrieved.

`RobotsDocument` holds the initial robots URL, rules text, terminal HTTP status
and a failure explanation. `allows(page_url)` first checks the page's scheme,
host and port against that initial source, including after a cross-origin redirect.
Its state determines the access decision.

| State | Retrieval result | Access decision |
| --- | --- | --- |
| `rules` | HTTP 200 or 204 | Evaluate the obtained text |
| `unavailable` | HTTP 404 or 410 | Permit the initial origin |
| `unreachable` | Other statuses, denied requests or transfer/redirect errors | Deny |

The project chooses 404/410 as the statuses that permit access without rules.
403, 429, 5xx and unexpected statuses block access. This uses the discretion for
unavailable files and the server/network-failure rule described in
[RFC 9309 access results](https://www.rfc-editor.org/rfc/rfc9309.html#section-2.3.1).
Future changes to this policy should update the loader tests together.

The body limit must be at least 500 KiB, following the protocol's minimum parsing
capacity. The default remains 2 MiB. Timeouts and body limits apply per request.
Transport/setup exceptions propagate as described for `fetch_http`.
Content-Type and encoding validation are future steps. `RobotsCache` retains these
origin-specific states as described below.

Six scenarios in `robots_loading_test.cpp` use the loopback server to cover rules,
empty/missing files, HTTP failures, download limits/timeouts, origin binding and
redirect failures. The HTTP CTest entry executes both client and robots-loading
scenarios.

### In-memory robots cache

`RobotsCache` owns the bootstrap request policy, HTTP options and origin-keyed
documents. `get(page_url)` returns a copy of fresh rules or calls `load_robots`
when the entry is missing or expired. Call `get` for each page permission decision
and combine `document.allows(page_url)` with the current host allowlist.

Defaults are 24 hours for `rules`/`unavailable` and one minute for `unreachable`.
`RobotsCacheOptions` accepts positive lifetimes with
`retry_after <= ttl <= 24 hours`. The TTL cap follows the caching guidance in
[RFC 9309 section 2.4](https://www.rfc-editor.org/rfc/rfc9309.html#section-2.4).
An expired entry refreshes at the next lookup. A failed refresh replaces the old
rules with the blocking result until the retry interval expires.

Freshness uses `steady_clock` starting when loading completes. Tests inject a
clock to check expiry boundaries without sleeping. Invalid page URLs produce a
blocking document and leave the cache intact. Returned values own their text,
so changing a returned document leaves cached rules intact. Loading exceptions
propagate before replacing an expired entry, allowing another attempt on the next lookup.

The cache supports a single-threaded crawl and lives for one process. Call `clear()`
to force reloads, including after changes to state captured by the bootstrap policy.
Fixed lifetimes currently govern expiry. HTTP cache validators, Cache-Control and
Retry-After handling can be added separately. Tests cover reuse across page paths,
origin isolation, missing-file caching, error recovery, failed refreshes and clear.

### Fetching pages under crawl policy

`PageFetcher` owns canonical allowed host names and a `RobotsCache` for a
single-threaded session. Construct it with host names produced by `http_url_host`.
Each page request checks host scope first, then the cached robots decision.
`fetch_http` applies that policy to the initial page and each redirect destination.
The cache uses the same host scope when retrieving robots files and following
their redirects. Host names are owned by the policy closure.

```cpp
hfs::PageFetcher pages{{"example.org", "docs.example.org"}};
auto result = pages.fetch("https://example.org/article");
```

`fetch` returns the existing `FetchResult`. Policy failures return
`FetchErrorKind::request_denied`, including a blocking robots state. Empty host lists
deny page requests before robots retrieval. Page HTTP options and the redirect
limit are supplied per call. `RobotsCacheOptions` in the constructor controls robots
downloads and cache lifetimes. Create a new session to replace its host scope.

Four local integration scenarios verify page blocking, cached decisions, missing
and failed robots retrieval, host rejection and redirects across origins. The
fixture records any request to its forbidden `/private` pages and fails the run
if one occurs. `UrlFrontier` supplies the bounded pending work described below.

### Host request timing

`HostDelay` tracks the last request attempt for each canonical host using
`steady_clock`. The first attempt is immediate. Later attempts wait for the
remaining part of `HostDelayOptions::interval`, which defaults to one second.
Time spent in a previous request counts toward the interval. HTTP/HTTPS and
different ports share timing for the same host name. Other host names have their
own timelines.

`PageFetcher` accepts `HostDelayOptions` as its third constructor argument. Its
robots bootstrap callback and page policy share a timer, covering redirects in
both request chains. Waiting occurs after access checks succeed. A denied page
with already cached rules consumes no new timing slot. Copies of the fetcher
share this timer through shared ownership, keeping stored callbacks valid after
copying or moving the session.

The interval is configured through `HostDelayOptions`. Zero permits immediate
requests for local tests. Negative intervals and empty clock/sleep callbacks
throw `std::invalid_argument`. Custom callbacks must supply monotonic time and
sleep for at least the requested duration. Built-in sleeping uses
`std::this_thread::sleep_for`; host waiting precedes the individual HTTP timeout.
This scheduler supports the existing single-threaded crawl model.

Five unit scenarios use an injected clock to verify first/next attempts, remaining
time, host grouping and invalid configuration. A local integration scenario checks
that two robots requests and three page requests share four intervals. Policy
tests configure zero delay, while this timing scenario advances a simulated clock.

### Bounded URL frontier

`UrlFrontier` owns canonical allowed host names, a FIFO of `CrawlTarget` values
and the set of URLs accepted during the session. `add_seed(url)` assigns depth
zero. `add_link(parent, reference)` resolves the reference against the parent URL
and assigns the next depth. Both paths use our URL normalization and host checks.
`pop()` returns an owning target or `std::nullopt` when the queue is empty.

`FrontierLimits` defaults to 1000 pending entries, 5000 accepted unique URLs and
depth 3. Zero maximum depth admits seeds only. Pending and total limits must be
positive. The total budget counts admitted candidates throughout the session,
including popped entries and eventual fetch failures. Corpus page targets are
tracked separately by the crawl loop.

`EnqueueResult` reports admission, a duplicate, an invalid URL, rejected host,
depth limit, full queue or exhausted total budget. Accepted URLs remain known
after popping, so links back to processed pages are rejected. Query strings and
path case keep their URL-module semantics. Capacity rejections leave candidates
eligible for later admission. Queue admission rolls back if dedup storage allocation
fails. `size()` counts pending work and `accepted_count()` counts unique admissions.

The frontier supports a single-threaded session. `PageFetcher` supplies robots,
HTTP and timing policy when processing a popped entry. The crawl loop joins these
components with HTML link discovery as described below.
Seven unit scenarios cover FIFO/depth, session deduplication, both capacity limits,
depth boundaries/overflow and rejected candidates.

### Bounded crawl loop

`crawl(seeds, allowed_hosts, consume, options)` runs one synchronous session using
`UrlFrontier`, `PageFetcher` and `HtmlDocument`. All seeds are admitted before
processing begins. The frontier resolves and filters discovered links; fetching
enforces host scope, robots rules, request delays and redirect limits.

```cpp
#include "hfs/crawl.hpp"
#include <iostream>

const std::vector<std::string> seeds{"https://example.org/"};
const auto summary = hfs::crawl(seeds, {"example.org"}, [](const hfs::CrawledPage& page) {
    std::cout << page.url << '\n' << page.title << '\n';
});
```

The required consumer receives one owning `CrawledPage` by const reference. It may
write immediately or copy the value for later use; its reference expires after
the callback. The loop retains the current document and URL bookkeeping rather
than accumulating every page body. Records include the final URL, title, body
text, discovery depth, HTTP status, Content-Type, requested redirect chain and
the `page_id`/`text_hash` values described below.

Only status 200 with media type `text/html` enters extraction and discovery.
Media type matching is case-insensitive and permits parameters. Missing types,
other media and other statuses are counted as skipped responses. Policy, transport
and redirect failures increment `fetch_failures` and processing continues without
automatic retries. Setup, allocation, parser and consumer exceptions propagate.

`CrawlOptions` groups existing frontier, HTTP, robots and timing settings. Its
positive `max_pages` (default 1000) bounds delivered HTML records; positive
`max_targets` (default 5000) bounds popped queue entries, including failures and
duplicates. This target budget does not count individual robots or redirect HTTP
requests. Each chain retains its existing request limits. `max_redirects` defaults
to five. `CrawlSummary` reports counts, rejected seeds (including duplicates),
remaining queue size and a stop reason: empty queue, page limit or target limit.
An empty queue takes precedence when no work remains at a limit boundary.

Relative links use the final response URL and retain the source's discovery depth
before adding one for a link. A session emits each final URL once. Already emitted
queued URLs are skipped before fetching; different redirect aliases can still
cause a repeated download before their shared destination is known. This is URL
deduplication; identical text at different final URLs is a later corpus concern.
Rejected discovery candidates retain the frontier's existing admission semantics.

The input gate below validates UTF-8 before extraction. The extractor returns
structural body text. Charset conversion, HTML `base`, main-article selection, language detection,
Text deduplication, detailed crawl logs and a global duration limit remain
separate increments. The CLI `crawl` command configures and invokes this library
entry point as described above.

Ten GoogleTest scenarios run under the local HTTP fixture in `crawler.crawl`.
They cover link discovery and cycles, robots/scope enforcement, redirect bases and
duplicate destinations, page/target/depth/queue limits, skipped media/statuses,
failure recovery, encoding rejection, page identities, seed rejection and consumer errors.
No internet is needed.

### HTML encoding input policy

`check_html_input(content_type, body)` returns `HtmlInputStatus`: `ready`,
`not_html`, `invalid_content_type`, `unsupported_encoding` or `invalid_text`.
The crawl loop applies it before creating a DOM or discovering links. Rejections
increment `skipped_responses` and processing continues with the next target.

The HTTP Content-Type must identify `text/html`. Its parameter parser handles
case-insensitive names, tokens, double-quoted values, escaped characters and empty
semicolon entries, using the syntax in [RFC 9110 section 5.6.6](https://www.rfc-editor.org/rfc/rfc9110.html#section-5.6.6).
Malformed parameters and repeated/empty `charset` parameters are rejected.
Whitespace around `=` is rejected. A `charset` substring inside another parameter's
quoted value is not an encoding declaration.

For a well-formed HTML Content-Type, encoding selection uses a BOM first, then an
HTTP charset, then the first recognized early HTML meta declaration. Lexbor scans
the first 1024 bytes for meta declarations and resolves encoding labels, including
its HTML prescan handling of UTF-16 meta labels. Comments do not declare encodings.
An unknown HTTP charset is rejected; unrecognized meta labels are ignored.

Only UTF-8 and its recognized aliases are accepted. UTF-16/32 BOMs and selected
legacy encodings are rejected without conversion. A UTF-8 BOM takes precedence
over a conflicting HTTP charset and is removed before DOM construction. Labels
such as `us-ascii` map to legacy encodings under Lexbor's web label table and are
also rejected. ASCII bytes with no declaration remain valid UTF-8 input.

When no encoding is selected, this crawler explicitly falls back to UTF-8. This
is a corpus policy, not a claim of full browser encoding detection. Every accepted
body is checked in full using Lexbor's UTF-8 decoder, including bytes beyond the
meta scan window. Invalid continuations, overlong sequences, surrogates, values
above U+10FFFF, truncation and NUL bytes cause rejection. No replacement or repair
is performed. The low-level `HtmlDocument` API retains its UTF-8 caller contract.

Seven unit scenarios cover metadata parsing, declaration precedence, meta scanning
and Unicode boundaries. The crawl fixture additionally verifies that rejected
bodies cannot supply links, later valid pages are delivered and a BOM stays out
of extracted text. These checks need no additional dependencies or internet.

### Stable page IDs and text hashes

`make_page_identity(final_url, text)` returns an owning `PageIdentity`, or
`std::nullopt` for an invalid, non-HTTP(S) or credential-bearing URL. It performs
no network requests. URL/parser setup, hashing backend and allocation failures
propagate as exceptions. The crawl loop computes this identity before delivering
each page, including pages reached through redirects.

| Field | Input to SHA-256 | Effect of a change |
| --- | --- | --- |
| `page_id` | URL serialized by `resolve_http_url(final_url, "")` | Same canonical URL keeps its ID when content changes |
| `text_hash` | Exact bytes of the extracted `body_text()` result | Changed extracted text changes its fingerprint; identical text at different URLs has the same fingerprint |

Both strings contain `sha256:` followed by 64 lowercase hexadecimal digits.
The page ID inherits the URL module's rules: fragments are removed, host/scheme
case and default ports normalize, while path case and query order remain distinct.
Redirect source URLs, titles, HTTP metadata, timestamps and session counters are
not hash inputs. The ID belongs to the final page URL.

Hashing adds no text normalization: whitespace/entity handling is already defined
by `HtmlDocument::body_text()`. Unicode normalization such as NFC, case folding,
extra whitespace collapsing and locale-dependent transformations are not applied.
The low-level identity helper hashes any byte string by its explicit length,
including an empty string or embedded NUL; the crawl input gate still validates
HTML before extraction. Stable corpus reproduction requires retaining the URL
and extraction rules. JSONL records retain `extractor_version` and
`url_normalization_version`; changes to those rules must update their version tags.

On Windows, SHA-256 uses [Windows CNG](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptcreatehash)
through the system `bcrypt` import library. Provider/hash handles use RAII and
each call owns its context. This adds no downloaded dependency. Other platforms
link `OpenSSL::Crypto` and use [EVP_Digest](https://docs.openssl.org/3.0/man3/EVP_DigestInit/),
alongside the existing OpenSSL requirement for their curl build. The Windows CNG
path is verified on MinGW GCC in Debug/Release; the non-Windows path has not been
built in this environment.

Seven unit scenarios check known SHA-256 vectors (including empty input and a
million-byte message), independent Python `hashlib` reference values for UTF-8,
binary data and a canonical URL, content changes, URL distinctions and equivalent
extracted text from different markup. The local crawl test checks identities
across redirect aliases and separate sessions. The CLI prints URLs and optionally
exports JSONL records. Text-based duplicate removal remains a separate increment.
