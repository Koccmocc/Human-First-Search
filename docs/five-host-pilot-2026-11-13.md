# Five-host diagnostic pilot: 2026-11-13

This run expands the initial two-host pilot to five independently hosted sites.
It checks real HTTPS/robots handling, redirects, English admission and extraction
on more layouts. It is a diagnostic sample, not the final 500–1000-page corpus
or a measured extraction/language accuracy benchmark.

## Repeating the run

The ordered seeds and limits are saved in
[`config/pilot-five-hosts.json`](../config/pilot-five-hosts.json). Create `data/`
first and select fresh output names:

```powershell
./build/release/crawler/hfs-crawler.exe crawl --config config/pilot-five-hosts.json --output data/five-hosts-corpus.jsonl --log data/five-hosts-log.jsonl --manifest data/five-hosts-run.json
python tools/verify_run.py data/five-hosts-run.json
```

The allowlist contains exactly `www.python.org`, `www.rfc-editor.org`,
`www.sqlite.org`, `www.postgresql.org` and `www.gnu.org`. Seeds alternate across
these hosts before discovered links. There are two initial URLs per host plus
one German GNU page as a negative language probe. The RFC `/about/` seed turned
out to redirect to the other RFC seed, providing a real duplicate-URL case.

Limits: 20 admitted unique-text pages, 50 targets, depth 1, 300 pending URLs,
400 total admitted URLs, a 180 s scheduling budget and a 1 s per-host interval.
Language policy is `en`. Existing default request timeouts/body limits, robots
cache and five-redirect limit were retained. Scope and robots checks stayed
enabled for all requests; no extra hosts were allowed to follow external links.

Windows Release used GCC 15.2.0, CMake 4.2.1 and Ninja 1.13.2, application 0.1.0
with `semantic-text-v2`, `lexbor-url-v1` and `cld2-compact-v1`. The source checkout
was `a101b23`; the manifest records tool/dependency versions, rather than a source
revision or executable fingerprint. Fetch completion times ranged from
`2026-11-13T20:11:26Z` to `2026-11-13T20:11:51Z`. Manifest elapsed time was 26583 ms,
measured before artifact hashing and manifest writing.

## Results

The CLI exited 0 and stopped at `page_limit`:

```text
pages=20 targets=22 fetch_failures=0 skipped_responses=0 empty_texts=0
duplicate_pages=1 duplicate_texts=0 non_english_pages=1 unknown_language_pages=0
rejected_seeds=0 pending_urls=289
```

| Host | Delivered pages | Selected containers |
| --- | ---: | --- |
| `www.python.org` | 13 | 7 `main_role`, 6 `article` |
| `www.rfc-editor.org` | 1 | 1 `main` |
| `www.sqlite.org` | 2 | 2 `body` |
| `www.postgresql.org` | 2 | 2 `body` |
| `www.gnu.org` | 2 | 2 `main_role` |

The German seed `https://www.gnu.org/gnu/gnu.de.html` returned valid HTML and was
logged as `non_english`, with detected code `de` and dominant-language share 95.
It did not enter the corpus or consume the admitted-page budget. The RFC alias
`https://www.rfc-editor.org/about/` reached the already processed
`https://www.rfc-editor.org/about/rfc-editor/` and produced `duplicate_url`.

`tools/verify_run.py` accepted corpus and journal fingerprints, UTF-8 JSONL framing,
schema versions, record totals and all outcome counters. An additional local
Python audit recomputed every URL-based page ID and exact-text SHA-256, checked
URL/text uniqueness, allowed hosts for every page request chain, status 200,
English metadata, processing versions, depth and UTC timestamps within the run.
Each corpus record matched its delivered journal event in order, URL, depth,
status and language metadata.

## Extraction review and limits

The start, middle and end of every delivered text were inspected. All delivered
records contained nonempty text and were classified as English; observed CLD2
shares were 97–99. This is not independent language ground truth, and text share
is not a confidence score.

- Python and GNU selected semantic regions. The RFC page retained a short
  in-section link list inside its selected main region.
- Both SQLite records used body fallback. Their starts retained the site's
  slogan, repeated menu links and search controls before the substantive text.
- Both PostgreSQL records also used body fallback, retaining the release banner
  and quick-link navigation before their page content.
- Some admitted pages are documentation/download directories or long release
  lists, rather than prose articles. English detection alone does not establish
  usefulness for the final search corpus.

The observed fallback noise is evidence for improving generic content selection
and reviewing seeds before increasing volume. No site-specific selector or
extraction-rule change was made in this increment, so processing versions stay
unchanged. Saving fewer characters alone would not demonstrate better quality.

All five hosts were reached through the ordered seeds in this run. The frontier
remains FIFO in DOM link order; after the seeds, Python supplied the remaining admitted pages.
This configuration therefore does not produce a balanced per-host sample or
guarantee coverage at larger limits. The final corpus needs deliberate source
sampling, further extraction review and a saved, verified snapshot; these 20
pages do not satisfy the 500–1000-page requirement.

## Local artifact identity

The producing stem is `data/five-hosts-20261113-211125`, with suffixes
`-corpus.jsonl`, `-log.jsonl` and `-run.json`. These files remain ignored locally.

| Artifact | Records | Bytes | Whole-file SHA-256 |
| --- | ---: | ---: | --- |
| Corpus | 20 | 169813 | `818f68a5b28f6776c97cf8f12e2709780937da3b05d68481b7dbd9437a6181b0` |
| Journal | 22 | 6974 | `178b55826a35e0d050768c0d2d7d18f8f77649329be35e258f4be286cdc625fc` |

Live content and timestamps can change, so these hashes identify this local run
and are not expected results for future live runs. This increment changes the
diagnostic configuration and documentation; the C++ implementation is unchanged.
