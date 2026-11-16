# Optional pending capacity per host

`--max-pending-per-host N` sets a positive upper bound on one canonical host's
pending URLs, across all depths. The library field is
`FrontierLimits::max_pending_per_host`; schema-1 config uses `max_pending_per_host`.
Omitting the option leaves it disabled. Zero, null and noninteger config values
are rejected. A value above the global cap is allowed; global limits still win.

For a 1000-entry global queue with a per-host cap of 200, one host can occupy at
most 200 pending slots. This leaves global room for other hosts while they are
below their own caps. The cap applies to seeds and discovered links, using the
resolved destination hostname. Schemes/ports share occupancy; allowed subdomains
remain separate. Popping a target frees its slot immediately, regardless of its
later fetch, language or deduplication outcome.

```powershell
./build/release/crawler/hfs-crawler.exe crawl --seed https://example.org/ --allow-host example.org --max-pending 1000 --max-pending-per-host 200
```

The new `config/pilot-100-host-cap.json` repeats the five-host 100-page profile
with a cap of 200. The original `config/pilot-100-pages.json` retains its disabled
default, providing a comparison profile. Use fresh output paths:

```powershell
./build/release/crawler/hfs-crawler.exe crawl --config config/pilot-100-host-cap.json --output data/host-cap-corpus.jsonl --log data/host-cap-log.jsonl --manifest data/host-cap-run.json
python tools/verify_run.py data/host-cap-run.json --check-pages
```

## Admission and accounting

The host check follows the existing depth, URL, scope, duplicate, total-URL and
global-pending checks. Thus `queue_full` or `url_limit` still wins when those
limits already reject a candidate. A host-only rejection returns
`EnqueueResult::host_queue_full` and increments `admission_host_queue_full` in
the CLI and manifest summary. It does not enter the known set and can be offered
again after space becomes available. There is no automatic retry backlog.

The cap does not evict existing targets, climb depth priorities or count completed
pages as occupancy. A seed can be rejected if its host's backlog is already full.
Counts are derived from existing host/depth queues, avoiding another mutable
index and its rollback rules. The extra scan is performed only when the option
is enabled and walks host/depth groups, not each URL.

Enabled manifests record the positive cap in `configuration` for config replay.
Disabled manifests omit that key, meaning no per-host cap. Scheduling's
`depth-host-round-robin-v1` version is unchanged: this is an explicit admission
setting, while target ordering and extraction/URL/language rules retain their
existing behavior.

The independent verifier accepts historical seven-counter admission groups and
the new optional eighth counter. If `admission_host_queue_full` is present it
must be a nonnegative integer and accompany the original complete group. It
also contributes to the total rejected attempts used to check `rejected_seeds`. Accepted admissions
still equal processed targets plus pending URLs.

## Tradeoffs and checks

This is an upper bound, not a guarantee of equal page counts or reserved turns.
With few active hosts, global capacity can remain unused rather than be borrowed
past a host's cap. Scope, language rejections, redirects and available links still
affect coverage. Total URL budget and global capacity can still reject candidates.
The cap deliberately bounds retained work; it does not eliminate lost candidates
or establish content quality.

GoogleTest cases check cross-depth occupancy, release/retry, canonical grouping,
global-check priority and invalid zero caps. Config/CLI tests check positive
values, malformed inputs and omission defaults. A two-host loopback crawl checks
that 12 versus 3 discovered children are limited to two retained children each,
and a CLI/config run checks recorded settings, counters and verifier acceptance.

All 165 CTest entries passed in Debug and Release on MinGW GCC with no new build
warnings. The verifier suite contains 17 Python scenarios within one CTest entry.

## Five-host 100-page comparison

`config/pilot-100-host-cap.json` was run with fresh output paths, keeping the
baseline's seeds, global limits, delays, scope, robots and language policy.
Only `max_pending_per_host: 200` was added to the profile. The process exited 0
at `page_limit`: 100 pages and 104 targets, distributed 21/19/21/20/19 by host.
The three fetch failures and one unknown-language result matched the prior run.

| Metric | Global limits only | With 200 pending per host |
| --- | ---: | ---: |
| Accepted admissions | 1104 | 1103 |
| Pending URLs at stop | 1000 | 999 |
| Duplicate offers | 8013 | 8203 |
| Global queue-full rejections | 8949 | 96 |
| Host queue-full rejections | disabled | 8664 |
| **Combined capacity rejections** | **8949** | **8760** |

Both runs completed 21122 admission offers. Invalid URL (272), out-of-scope
(2784), depth-limit (0) and total-URL-limit (0) counts were unchanged. The new
accounting relation holds: 1103 accepted = 104 processed + 999 pending.

The global queue-full count alone is misleading here: most capacity rejections
moved to the explicit host-cap category. Combined rejections fell by 189 offers,
not by 8853 unique pages. This demonstrates the configurable upper bound and its
diagnostics; it does not prove fewer unique losses or better corpus quality.

All 100 delivered URLs, page IDs and text hashes matched the baseline.
`verify_run.py --check-pages` accepted the new settings, metadata, fingerprints
and accounting. Observed elapsed time was 72178 ms before hashing/manifest
writing; separate live runs are not a performance benchmark.

Local artifact stem: `data/pilot-100-host-cap-20261116T162220Z`, with the existing
corpus/journal/manifest suffixes. Corpus: 100 records, 1294870 bytes, SHA-256
`5e3c2c155f970ee32b5639190dffa51dffdcbd0d5a8ba5fa7d75da42152a3351`.
Journal: 104 records, 34216 bytes, SHA-256
`9dbbaf185957a7a7d47d4900a5f5f73436586686bc367a04bae2d47eca2a7d19`.
Artifacts and one-off analysis remain ignored locally. Future live results can vary.
