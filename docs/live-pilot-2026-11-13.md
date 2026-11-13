# Live pilot: 2026-11-13

This small Windows/MinGW pilot exercises real HTTPS, robots loading, host scope,
delays, HTML extraction, English admission, hashing and JSONL/journal output.
It is an initial diagnostic run, not the final 500–1000-page corpus or an accuracy
benchmark. Raw corpus, journal and inspection files stay in ignored local directories.
Toolchain: GCC 15.2.0 (MSYS2 MinGW), CMake 4.2.1 and Ninja 1.13.2.

The branch was renamed from `liubomyr/crawler` to `crawler` in the team repository.
Subsequent changes are published to the previous repository's `main` and the
team repository's `crawler`.

## Configuration and observations

Seeds: [Python about](https://www.python.org/about/) and
[RFC Editor about](https://www.rfc-editor.org/about/rfc-editor/).
Only `www.python.org` and `www.rfc-editor.org` were allowed. Their robots files
were inspected and fetched by the crawler's normal policy. Both runs used Release,
default English admission, 1 s per-host delay, depth 1, 8 accepted pages, 24 target
attempts, 100 pending/total admitted URLs, and a 60 s scheduling budget. Existing
HTTP limits remained 3 s connect / 10 s total per request, 2 MiB and five redirects.

Baseline was commit `a934125` with `semantic-text-v1`. Fetch completion timestamps
ran from `2026-11-13T16:39:04Z` to `2026-11-13T16:39:12Z`.
The updated extractor was fetched from `2026-11-13T16:47:09Z` to
`2026-11-13T16:47:17Z` using the same ordered configuration.

Both runs completed with exit 0 and:

```text
stop=page_limit pages=8 targets=8 fetch_failures=0 skipped_responses=0
empty_texts=0 duplicate_pages=0 duplicate_texts=0 non_english_pages=0
unknown_language_pages=0 rejected_seeds=0 pending_urls=92
```

The frontier reached its configured 100-URL admission cap; the remaining queue
was deliberately not processed. Of the eight accepted pages, seven belonged to
Python.org and one to RFC Editor. This small FIFO run does not establish balanced
domain coverage, rejection quality, or behavior on failures.

## Finding and fix

Five baseline records used body fallback and included outer site controls and
service notices. Inspecting the Python.org HTML showed a semantic content region
marked `<section role="main">`, without a usable native main/article on those pages.

`semantic-text-v2` adds exact single-role main selection after native main/article
selection and before body fallback. Existing clean article/main selection stays
preferred. The [ARIA main landmark](https://www.w3.org/TR/wai-aria-1.2/#main)
motivates this narrow heuristic; full role-token resolution is outside this change.
The selected branch is exported as `text_source: main_role`.

| URL | Before → after source | Text Unicode scalars before → after |
| --- | --- | --- |
| `https://www.python.org/about/` | body → main_role | 2678 → 2206 |
| `https://www.rfc-editor.org/about/rfc-editor/` | main → main | 5461 → 5461 |
| `https://www.python.org/` | body → main_role | 4860 → 2379 |
| `https://www.python.org/psf-landing/` | body → main_role | 2318 → 1943 |
| `https://www.python.org/jobs/` | body → main_role | 3670 → 3350 |
| `https://www.python.org/community/` | body → main_role | 2183 → 1562 |
| `https://www.python.org/community/irc/` | article → article | 1387 → 1387 |
| `https://www.python.org/about/apps/` | article → article | 2685 → 2685 |

All eight URLs and URL-based page IDs matched between runs. Five text hashes
changed, corresponding to the five changed selections; the other three text
hashes stayed equal. Total extracted scalar count dropped from 25242 to 20973.
Inspection of the new text starts confirmed removal of the outer controls on
these pages. Less text alone is not a general content-quality metric, and the
live pages were fetched at different times rather than from a frozen snapshot.

An independent Python check parsed every JSONL record as UTF-8, checked LF/no BOM,
recomputed URL/text SHA-256 values with hashlib, verified unique URLs and text
hashes, allowed hosts, status 200, English metadata, valid UTC timestamps and the
one-to-one page-delivered journal events. It checked both eight-record exports.
All 131 CTest entries pass in Debug and Release, including synthetic role-main
regressions and the CLI export/discovery test.

## Local artifacts and repeat command

Local files are `data/pilot-2026-11-13-{corpus,log}.jsonl` and
`data/pilot-2026-11-13-v2-{corpus,log}.jsonl`. They are not committed.
Corpus SHA-256 values (whole file, including fetch timestamps):

- Before: `e19746c831d4770c44e963e8186f5c46cc013fa66cb37ae1524286fd69c8c95e`
- After: `87d8250ee88cae532457cd1420de3f24dfe4aa5e801aa775d3fc349679a044e6`

Choose fresh output names before repeating, since existing files are rejected:

```powershell
./build/release/crawler/hfs-crawler.exe crawl --seed https://www.python.org/about/ --seed https://www.rfc-editor.org/about/rfc-editor/ --allow-host www.python.org --allow-host www.rfc-editor.org --max-pages 8 --max-targets 24 --max-depth 1 --max-pending 100 --max-urls 100 --max-duration-ms 60000 --delay-ms 1000 --output data/new-pilot-corpus.jsonl --log data/new-pilot-log.jsonl
```

Live content and fetched_at values can change. The hashes identify these local
artifacts; this pilot does not yet supply the final reproducibility manifest.
Broader sampling should inspect mixed-language pages, other layouts, short/failed
responses and boilerplate inside selected regions before expanding the corpus.

The same settings are now saved in `config/pilot.json`; see
[configuration usage](crawl-config.md) for repeating them with `--config`.

## Saved-run verification follow-up

A subsequent Release run used `config/pilot.json` with fresh corpus, journal and
`--manifest` destinations. It again stopped at `page_limit`: 8 pages, 8 targets,
no fetch failures or skipped/empty/duplicate/language-rejected pages, and 92
pending URLs. `tools/verify_run.py` accepted the saved run independently:

| Artifact | Records | Bytes | SHA-256 |
| --- | --- | --- | --- |
| Corpus | 8 | 26383 | `c138e2b308648e6f6f7b2f8cf2afc38fce4d079ff43879697afbf7f3a96d5b5a` |
| Journal | 8 | 2534 | `fef3b6ca86a635ce4cd8a22b2851c0b667e9277188de8503a8b09b52e0b825f5` |

The verifier checked exact file fingerprints, JSONL records and all journal
outcome counts against the manifest. This repeats the same small scope and does
not expand the earlier content-quality sample. New artifacts remain local under
ignored `data/`. All 144 CTest entries passed in Debug and Release, including
seven verifier corruption/portability scenarios and real CLI fixture checks.
