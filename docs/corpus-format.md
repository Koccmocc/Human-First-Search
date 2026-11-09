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
| `extractor_version` | `body-text-v1`: current `HtmlDocument::body_text()` rules |
| `url_normalization_version` | `lexbor-url-v1`: current HTTP(S) URL resolution/serialization rules |
| `page_id` | `sha256:` plus the SHA-256 of the canonical final URL |
| `url` | Canonical final HTTP(S) URL, without fragment |
| `title` | Extracted title; may be empty |
| `text` | Extracted structural body text; may be empty |
| `text_hash` | `sha256:` plus the SHA-256 of the exact `text` bytes |
| `language` | `null`: language detection has not been implemented |
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

`language` is unknown until language detection is implemented. Each delivered
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

Structural body text can still contain navigation/footer content. Text duplicates
at different URLs are retained.
This export is an intermediate data format before article selection, language
filtering, text deduplication and the final reproducibility manifest are complete.

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
