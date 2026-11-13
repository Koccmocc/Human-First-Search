# Saved crawl configuration, schema version 1

Run from the repository root, with fresh output paths and an existing output directory:

```powershell
./build/release/crawler/hfs-crawler.exe crawl --config config/pilot.json --output data/config-pilot-corpus.jsonl --log data/config-pilot-log.jsonl
```

The committed pilot config contains the ordered seeds, allowed hosts and limits
from the first live pilot. Output and log paths are supplied separately so the
same crawl settings can be reused without reusing existing files.

| Key | Type and meaning |
| --- | --- |
| `schema_version` | Required integer `1` |
| `seeds` | Required nonempty array of absolute credential-free HTTP(S) URL strings; order is preserved |
| `allowed_hosts` | Required nonempty array of host-name strings, with bracketed IPv6 allowed |
| `max_pages` | Optional positive integer, same as `--max-pages` |
| `max_targets` | Optional positive integer, same as `--max-targets` |
| `max_depth` | Optional nonnegative integer, same as `--max-depth` |
| `max_pending` | Optional positive integer, same as `--max-pending` |
| `max_urls` | Optional positive integer, same as `--max-urls` |
| `max_duration_ms` | Optional positive integer, same scheduling budget as `--max-duration-ms` |
| `delay_ms` | Optional nonnegative integer, same as `--delay-ms` |
| `language` | Optional string `en` or `any`, default `en` |

Omitted options use the same defaults as direct CLI mode. The pilot file sets
every supported crawl option explicitly. HTTP/robots transport settings and
redirect limits retain the current library defaults; this schema does not expose
them yet. Numeric strings, floats (including 1.0), booleans, negative counts and
out-of-range values are rejected. JSON must be UTF-8; an optional UTF-8 BOM is
accepted. Comments and trailing data are not accepted.

Unknown or duplicate keys are errors, including duplicate keys with escaped
spellings. Nested objects and nested arrays are unsupported. The maximum file
size is 64 KiB; the reader checks it with a bounded read. Scope, URL, host and
numeric validation is reused from the existing CLI parser. No shell commands
are constructed from config values.

With `--config`, only `--output` and `--log` may accompany it. Mixing direct
seeds, hosts or limit flags with a file is rejected instead of defining hidden
override precedence. Paths are relative to the process's current working
directory, including the config file path. Output paths are not config keys.

CLI parsing itself performs no file access. Main loads and validates the file
before opening outputs or starting requests. Missing, unreadable, malformed or
invalid config files produce exit 2. Existing output paths remain exit 1 and are
never overwritten, including a path pointing at the config itself. Allocation
or other fatal runtime exceptions remain exit 1. The input file is not modified.

`parse_crawl_config(json)` is the owning, in-memory API; `load_crawl_config(path)`
adds bounded file reading. Both return `CrawlCommand` or `CliError` and perform
no crawling. Six unit scenarios and process tests cover validation, size bounds,
ownership, CLI equivalence, failures before requests and preservation of files.

Saved parameters do not freeze live web content, capture effective dependency
versions or produce a run manifest. Those remain separate parts of corpus
reproducibility; independent live runs can have different content and timestamps.
