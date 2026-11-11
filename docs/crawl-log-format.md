# Crawl log, schema version 1

`crawl --log PATH` writes compact UTF-8 JSONL without BOM, one LF-terminated
record per completed queue target. It works with or without `--output`. Corpus
records and log records have separate files and contracts.

| Field | Meaning |
| --- | --- |
| `schema_version` | Integer `1`, version of this log format |
| `event` | `crawl_target` |
| `url` | Canonical URL popped from the queue, before redirects |
| `depth` | Discovery depth; seeds have depth zero |
| `outcome` | `page_delivered`, `fetch_failed`, `response_skipped`, `empty_text`, `duplicate_url` or `duplicate_text` |
| `final_url` | Known final URL, or `null` on fetch failure |
| `http_status` | Final response status, or `null` when no response is available or no fresh fetch occurred |
| `html_input` | Input-gate result for status 200; otherwise `null` |
| `error` | `null`, or an object with `category`, `kind` and `message` |

`html_input` uses `ready`, `not_html`, `invalid_content_type`,
`unsupported_encoding` or `invalid_text`. Error category `http` uses
`invalid_request`, `timeout`, `body_limit` or `transport`; category `fetch` uses
`invalid_url`, `request_denied`, `redirect_loop` or `redirect_limit`. Messages are
diagnostics and may vary between platforms; use category/kind for program logic.
Permission denials still combine scope and robots checks as `request_denied`.

In a normally completed session, the number of log lines equals the summary's
`targets` count. Outcome counts match `pages`, `fetch_failures`,
`skipped_responses`, `empty_texts`, `duplicate_pages` and `duplicate_texts`.
Events retain queue processing order. A successful page event follows consumer
delivery and link discovery. Pending targets, rejected seeds/links and individual
HTTP/robots requests are outside this stream. It contains no bodies, event
timestamps, partial redirect chains on failures or duplicate-representative map.

Destinations must be new files with existing parent directories. Existing files,
directories and symlinks are rejected before crawling. Pointing `--output` and
`--log` at the same file fails before requests. Files open sequentially, so an
opening failure can leave the first new file empty. No atomic completion is
promised. Serialization, write and close errors abort with exit 1; a failed run
may leave partial files or a partial final line. Always check process completion.

The library function `write_crawl_event_jsonl(stream, event)` uses nlohmann/json
to validate UTF-8 and escape strings before writing each record. It preserves
supplied values without checking semantic consistency between fields. It owns
neither the event nor the stream; callers flush/close their stream. Repeated
serialization of the same event produces identical bytes.
