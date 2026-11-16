# Filtered corpus handoff: 2026-11-16

This run exercises URL export selection on the same six-host profile as the
[unfiltered 500-page snapshot](pilot-500-pages-2026-11-16.md). The crawler still
fetches excluded supported HTML and discovers its links. Selection changes which
texts are exported, without changing extraction, URL identity or text hashing.

## Profile and replay

[`config/pilot-500-filtered.json`](../config/pilot-500-filtered.json) has the same
12 ordered seeds, scope, 500-page/1500-target budgets, depth 2, 3000 pending URLs,
500 pending per host, 5000 admitted URLs and fifteen-minute scheduling budget.
One-second host delays, robots, redirect checks, English admission and HTTP/body
limits remain enabled. The only config difference is five `export_exclusions`:

- `https://www.python.org/jobs/*`
- `https://www.python.org/downloads/*`
- `https://www.kernel.org/doc/html/latest/genindex.html`
- `https://www.postgresql.org/download/*`
- `https://www.rfc-editor.org/subjects/*`

These are explicit corpus-selection choices, not universal category detection.
Exact matching and directory/query behavior are specified in
[export selection](export-selection.md).

From the repository root, create `data/` and choose fresh output paths:

```powershell
./build/release/crawler/hfs-crawler.exe crawl --config config/pilot-500-filtered.json --output data/filtered-corpus.jsonl --log data/filtered-log.jsonl --manifest data/filtered-run.json
python tools/verify_run.py data/filtered-run.json --min-pages 500 --min-hosts 5
python tools/package_run.py data/filtered-run.json --output data/indexer-handoff-filtered-500.zip --source-commit 2f598b71c8a4a8aa2d1e8550e4ce602bd6b6a828
```

That source revision is the producing crawler code for this saved run; use the
actual revision for another run. The manifest does not bind artifacts to a Git
or binary hash, so the source declaration remains caller-supplied. This increment
changes the report/handoff documentation, not the crawler's behavior.

## Observed results

The process exited 0 at `page_limit`: **500 pages**, 798 targets
and 601890 ms elapsed before artifact hashing/manifest writing.
`verify_run.py --min-pages 500 --min-hosts 5` passed, including page-content audit.
Fetch times span `2026-11-16T19:47:19Z` through `2026-11-16T19:57:19Z`.

| Delivering host | Pages | Unicode scalar values | Selected branches |
| --- | ---: | ---: | --- |
| `www.gnu.org` | 112 | 1200254 | 4 `body`, 28 `heading`, 80 `main_role` |
| `www.kernel.org` | 108 | 1645130 | 1 `heading`, 107 `main_role` |
| `www.postgresql.org` | 75 | 544895 | 23 `body`, 52 `heading` |
| `www.python.org` | 68 | 273977 | 55 `article`, 13 `main_role` |
| `www.rfc-editor.org` | 45 | 1280052 | 45 `main` |
| `www.sqlite.org` | 92 | 1721084 | 61 `body`, 31 `heading` |

Largest page share: 22.4%.
Delivered depths: 0: 12, 1: 289, 2: 199.
Total extracted length: 6665392 characters. Text lengths are
205 minimum, 4571.0 median,
34737 nearest-rank p90 and 495171 maximum.
22 records are shorter than 500 characters;
0 titles are empty. These are observations, not quality thresholds.

```text
pages=500 targets=798 excluded_pages=106
fetch_failures=39 skipped_responses=7 empty_texts=0
duplicate_pages=8 duplicate_texts=9
non_english_pages=97 unknown_language_pages=32
rejected_seeds=0 pending_urls=2376
```

Fetch error categories: 21 `fetch:request_denied`, 15 `http:body_limit`, 3 `http:timeout`.
Policy denials do not identify the particular scope/robots check or establish a
permanent site policy. Excluded targets still consume request/time budgets.

```text
admission_added=3174
admission_depth_limit=45202
admission_duplicate=39243
admission_host_queue_full=22174
admission_invalid_url=871
admission_out_of_scope=8112
admission_queue_full=0
admission_url_limit=0
```

Admission accounting holds: 3174 accepted = 798
processed + 2376 pending. Rejections count offers, which can
repeat URLs; they do not measure unique lost pages.

An earlier invocation (`data/filtered-500-20261116T193055Z-run.json`) stopped
without completing its manifest. Those partial files were retained separately
and are not included in this snapshot or its accounting.

## Comparison and content review

The unfiltered baseline has 500 pages and 7514290
characters; this run has 500 pages and 6665392 characters.
The only configuration difference is the five export exclusions. Separate live
runs also differ in content/availability, so this is not a controlled extraction
accuracy or timing benchmark.

| URL-set comparison | Count |
| --- | ---: |
| Baseline URLs matching the new rules | 102 |
| New exported URLs matching the rules | 0 |
| URLs shared by both corpora | 387 |
| Newly delivered URLs absent from baseline | 113 |
| Baseline URLs absent from new snapshot | 113 |
| Shared URLs with changed text hashes | 8 |

All shared URLs retain their page IDs. Changed text hashes can reflect live
content changes; snapshot absence does not establish deletion. Baseline source
counts: {"article": 61, "body": 89, "heading": 99, "main": 103, "main_role": 148}.
Filtered source counts: {"article": 55, "body": 88, "heading": 112, "main": 45, "main_role": 200}.

The actual C++ matcher independently checked every new exported URL (zero
matches) and every `export_excluded` final URL (all matched). Excluded events
retain status 200, supported HTML, no fetch error and `language: null`.
Excluded event counts by final host: {"www.kernel.org": 1, "www.postgresql.org": 15, "www.python.org": 35, "www.rfc-editor.org": 55}.
The Python verifier also checked the recorded rules and outcome totals. The
additional read-only audit matched all delivered rows to their journal events,
recomputed ID/text hashes and checked request chains, language and UTC times.

Thirty-eight distinct final-corpus records were reviewed at their start (250
characters), middle (200) and end (200): first/last per delivering host, two
shortest and the longest per extraction branch, first depth-2 page per host,
and five SQLite follow-ups. These are deterministic samples, not a measured
accuracy/relevance score for all 500 pages.

- The excluded Kernel symbol index and the configured Python job/download,
  PostgreSQL download and RFC subject paths are absent. English selection still
  admits other directories: GNU link lists, Python success-story categories,
  PostgreSQL documentation/professional-service lists and short Kernel indexes.
- SQLite body fallbacks retain their repeated menu/search prelude, including the
  FTS5 and shared-cache documentation. PostgreSQL documentation retains release
  banners, version navigation and report-form text. A short conference photo-page
  instruction remains outside the configured exclusions.
- Useful technical prose is present in Kernel CPU operations, PostgreSQL policy,
  individual RFCs and SQLite PRAGMA/features samples. Kernel language-choice labels
  and RFC display/subscription controls remain inside selected regions.
- The largest remaining record is Kernel API documentation at 495171 characters.
  Long-document handling is still required by the indexer's tokenizer/chunker.
  URL selection does not impose an embedding-model token limit.

The 106 excluded events count processed targets. Because selection precedes
language analysis and text deduplication, they are not 106 proven unique English
documents that would otherwise have been delivered. This snapshot meets the
minimum volume/coverage and configured URL policy; further text curation and the
real indexer's ingestion/update/search checks remain separate work.

## Saved handoff

Producing manifest: `data/filtered-500-20261116T194717Z-run.json`. Its corpus, journal, original
configuration, source declaration, audit/acceptance outputs and captured process
output remain under ignored `data/`. Analysis and sample selections remain under
ignored `build/` at `filtered-review.json`, `filtered-comparison.json` and
`filtered-independent-audit.json`.

| Artifact | Records | Bytes | SHA-256 |
| --- | ---: | ---: | --- |
| corpus | 500 | 7111187 | `4463514506020b8c9c5996ead7195c353779d3d9fadabbe64b236e15c1d946fb` |
| journal | 798 | 267312 | `372307a0c0411584d3953ed23b38e197255f9e0cd937bd5364a300cb539e64fe` |
| manifest | 1 | 2808 | `4c0ceb106ef6a30727cbdc40281801fc4a2e5a80ec3b37495542e682e576daa9` |

The local transfer archive is `data/indexer-handoff-filtered-500.zip`. The copied
corpus/journal/manifest bytes are unchanged. After unpacking to a different
directory, the bundled verifier passed both integrity/page checks and the
500-page/five-host minimums. The documented streaming reader read all 500 records.
Raw data and the transfer archive are not tracked in Git.

Offline consumption of these saved bytes is repeatable; future HTTP crawls may
produce different contents and coverage. Acceptance by the actual indexer and
native Linux/macOS crawler builds remain separate unverified work.
