# Draft corpus format, schema version 1

One UTF-8 JSON object per line; no BOM, no array wrapper. The CLI opens its file
in binary mode and terminates each record with LF on Windows and Unix. Quotes,
backslashes and control characters inside strings are escaped by nlohmann/json.
The serializer validates UTF-8 before writing a record and produces deterministic
compact JSON for the same `CrawledPage` values. No timestamps are generated during
serialization. This contract is provisional until team integration.

| Field | Current meaning |
| --- | --- |
| `schema_version` | Integer `1` |
| `extractor_version` | `semantic-text-v2`: current `HtmlDocument::main_text()` rules |
| `url_normalization_version` | `lexbor-url-v1`: current HTTP(S) URL resolution/serialization rules |
| `page_id` | `sha256:` plus the SHA-256 of the canonical final URL |
| `url` | Canonical final HTTP(S) URL, without fragment |
| `title` | Extracted title; may be empty |
| `text` | Selected semantic content or filtered body fallback; nonempty for crawl output |
| `text_source` | Selected container: `main`, `article`, `main_role` or `body` |
| `text_hash` | `sha256:` plus the SHA-256 of the exact `text` bytes |
| `language` | Detected CLD2 code, normally `en`; `null` for unknown text admitted in diagnostic mode or absent analysis |
| `language_percent` | Dominant-language share of classified text; `null` if no code is available |
| `language_detection_version` | `cld2-compact-v1` when analysis ran; otherwise `null` |
| `fetched_at` | Captured UTC completion time as `YYYY-MM-DDTHH:MM:SSZ`; `null` if no time was captured |
| `http_status` | HTTP response status, currently `200` for delivered pages |
| `content_type` | Original response Content-Type value |
| `depth` | Link discovery depth, with seeds at zero |
| `requested_urls` | Ordered page request chain, including initial and final URLs |

The two hashes contain 64 lowercase hexadecimal digits after their prefix.
Neither title, timestamps nor other metadata affects them. Text normalization
belongs to extraction; hashing and serialization do not additionally normalize
Unicode or whitespace. Bump the corresponding version tag when extraction or
URL normalization rules change.

`semantic-text-v2` chooses the first nonempty HTML `main`, otherwise the first
nonempty HTML `article`, otherwise the first nonempty HTML element with exact
`role="main"`, otherwise body fallback. The role branch reports `main_role`.
It trims HTML ASCII whitespace and accepts only the single lowercase role;
role lists and broader ARIA role resolution are not implemented.
It skips script/style/template,
nav/aside/footer and `hidden` subtrees; normalization retains the original body
whitespace/entity rules. Content headers are retained. Container emptiness is
checked after filtering. Only HTML-namespace containers qualify.

`text_source` exposes the chosen branch. The source `body` still applies the
semantic skip rules and can retain unmarked site chrome. This is a tag-based
heuristic without CSS, general ARIA-role resolution or content-quality scoring. Earlier
exports used `body-text-v1` and had no `text_source`. New selection can change
`text_hash` for the same URL, while `page_id` remains URL-based. All-document link
discovery is independent of the selected text region. `semantic-text-v1` used
main/article/body only; v2 can change text hashes for former body-fallback pages
while leaving their URL-based IDs unchanged.

Default `--language en` admits only `LanguageStatus::detected` with code `en`.
Detection uses selected main text with neutral hints. Its fixed bounds are
200 Unicode scalar values minimum and 2 MiB maximum. Empty text is rejected
separately. Unknown results (including too-short, unreliable, invalid or oversized
input) and detected non-English text do not enter the corpus, but still supply
links under normal frontier policy. Only admitted unique texts use `max_pages`.

`--language any` is diagnostic mode: all nonempty texts are eligible, with their
actual detection metadata. Language rejection counters stay zero in this mode.
`language_percent` is text share, not a confidence probability, and this initial
admission policy adds no further percentage threshold. Detection is statistical;
an admitted page can still contain passages in other languages.

Manually constructed records may have no analysis; all three language fields
then serialize as `null`. Earlier schema-1 exports may lack the new metadata
fields or have `language: null` without analysis. These additive fields do not
change extraction, URL identity or text-hash rules.

Each delivered
page's `fetched_at` is observed with `system_clock` immediately after the fetcher
returns its final response, before HTML validation, extraction, hashing and the
consumer. Intermediate redirects do not get separate record timestamps. Formatting
uses UTC calendar arithmetic and the classic numeric locale, with fractions
floored to whole seconds. It uses no local-time or operating-system formatting API.

`CrawledPage` retains an optional typed instant. A manually constructed page or an
older export may have no captured time, represented as `null`. Serialization
preserves that absence rather than inventing a time. `CrawlOptions::wall_now`
defaults to `system_clock::now` and can be injected for deterministic tests; an
empty callback is invalid. Clock exceptions abort the session. Host delays still
use their separate `steady_clock` and do not depend on these wall-clock values.

Independent live fetches can have different timestamps. Serializing the same
saved record remains deterministic, while comparing separate live exports must
account for `fetched_at`. Page IDs and text hashes remain independent of time.

The crawl loop delivers only the first page per `text_hash` during its session.
Representative choice follows ordered seeds, FIFO processing and DOM link order;
the same configuration and fetch outcomes reproduce the choice. Different seed
order can choose a different URL/title/metadata. Text duplicates still supply
links to the frontier, subject to the existing crawl limits and access policy.
The index retains SHA-256 fingerprints, not copies of corpus texts. The standalone
JSONL writer has no deduplication state and writes each supplied record.

The crawl loop rejects empty extracted texts before hashing or delivery. Emptiness
is checked after semantic filtering and whitespace normalization; a title alone
does not qualify. Each newly processed final URL rejected this way increments
`empty_texts`, independently of response skips and text duplicates. Its links
still enter the frontier under normal policy and limits. Empty pages use the
target budget but not the delivered-page budget. An all-empty session creates
an empty JSONL file and completes normally.

Empty-text admission itself does not change extraction rules or the schema.
The standalone serializer can still write an empty text supplied by its caller;
earlier crawl exports may contain one empty-text representative. Short nonempty
texts remain eligible in diagnostic any-language mode. Unmarked boilerplate may
remain in selected text. This export is an intermediate data format before
real-site content-quality evaluation and the final reproducibility manifest are complete. Detailed
mapping from skipped URLs to their representatives remains a future journal extension.

`write_page_jsonl(output, page)` accepts a page produced by the crawl loop. It
preserves the supplied fields and does not recompute IDs, validate their semantic
consistency or own the output stream. It serializes the current record in memory
before writing, so malformed UTF-8 does not append part of that record. The caller
must flush or close its stream. Serialization/allocation and output errors propagate.
I/O failure can leave a partial final line; callers must check process completion
before treating an export as complete.

An independent Python reader can iterate over the file without loading the whole
corpus:

```python
import json

with open("data/corpus.jsonl", encoding="utf-8") as corpus:
    for line in corpus:
        page = json.loads(line)
        print(page["page_id"], page["url"])
```
