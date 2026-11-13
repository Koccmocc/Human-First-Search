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
| `outcome` | `page_delivered`, `fetch_failed`, `response_skipped`, `empty_text`, `duplicate_url`, `duplicate_text`, `non_english` or `unknown_language` |
| `final_url` | Known final URL, or `null` on fetch failure |
| `http_status` | Final response status, or `null` when no response is available or no fresh fetch occurred |
| `html_input` | Input-gate result for status 200; otherwise `null` |
| `error` | `null`, or an object with `category`, `kind` and `message` |
| `language` | Analysis object with `status`, `code`, `percent`, `detector_version`, or `null` if analysis was not reached |

`html_input` uses `ready`, `not_html`, `invalid_content_type`,
`unsupported_encoding` or `invalid_text`. Error category `http` uses
`invalid_request`, `timeout`, `body_limit` or `transport`; category `fetch` uses
`invalid_url`, `request_denied`, `redirect_loop` or `redirect_limit`. Messages are
diagnostics and may vary between platforms; use category/kind for program logic.
Permission denials still combine scope and robots checks as `request_denied`.

In a normally completed session, the number of log lines equals the summary's
`targets` count. Outcome counts match `pages`, `fetch_failures`,
`skipped_responses`, `empty_texts`, `duplicate_pages`, `duplicate_texts`,
`non_english_pages` and `unknown_language_pages`.
Events retain queue processing order. A successful page event follows consumer
delivery and link discovery. Pending targets, rejected seeds/links and individual
HTTP/robots requests are outside this stream. It contains no bodies, event
timestamps, partial redirect chains on failures or duplicate-representative map.

Language analysis follows nonempty main-text extraction and precedes hashing.
`non_english` means a reliable detected code differs from `en`.
`unknown_language` reports `too_short`, `unreliable`, `invalid_text` or `too_large`
in `language.status`; these results have `code: null` and `percent: 0`.
The detector version is `cld2-compact-v1`. A detected language's percent is its
share of classified text, not a confidence probability. Rejected pages still
supply links. English-only admission is the default; `--language any` still
records analysis but never emits these two rejection outcomes. Empty text,
pre-extraction failures and already processed final URLs leave `language` null.
Schema-1 logs written before language integration lack this optional field.

When `--max-duration-ms` stops scheduling new targets, completed targets remain in
the log and pending URLs do not receive events. The summary reports
`stop=duration_limit`, exit status is 0 and both output streams close normally.
The current target is allowed to finish even when its work exceeds the budget.

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
