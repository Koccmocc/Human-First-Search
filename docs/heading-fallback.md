# Heading fallback: semantic-text-v3

The [five-host pilot](five-host-pilot-2026-11-13.md) found menu/banner text in
body fallback on SQLite and PostgreSQL. Their About pages contain a clear `h1`
before substantive content, but no usable `main`, `article` or `role="main"`.
Version v3 adds a structural fallback after those semantic branches, before body.

## Rule and tradeoff

There must be exactly one HTML `h1` outside the existing skipped subtrees. Its
immediate parent must be HTML `body`, `div` or `section`. The heading must produce
nonempty normalized text. Its following siblings, extracted within that parent
with the same semantic filters, must produce at least 200 UTF-8 bytes. The result
is the heading, a newline, and that following text, labelled `text_source: heading`.

The suffix byte gate avoids choosing a title-only wrapper or short teaser. It is
independent of CLD2's minimum Unicode-scalar count and does not establish prose
quality. Multiple h1s and isolated title wrappers retain body fallback. Traversal
does not climb through wrappers or inspect CSS classes, IDs, host names or CSS
visibility. DOM walks are iterative, with a fixed number of linear passes.

Text before the h1 and outside its parent is excluded; this can omit useful
introductions or other sections on unfamiliar layouts. Unmarked navigation
inside the selected range can remain. This is a conservative heuristic, not a
general readability algorithm. Native/role selection keeps its priority and
`body_text()` keeps its existing behavior. All-document link discovery is
independent of text selection.

## Controlled snapshot comparison

The old and new extractor processed the same two saved HTML byte sequences.
Files remain local under ignored `build/extractor-review/`.

| Page | v2 bytes | v3 bytes | Selection |
| --- | ---: | ---: | --- |
| SQLite About | 4189 | 3993 | `body` to `heading` |
| PostgreSQL About | 6644 | 6396 | `body` to `heading` |

SQLite HTML SHA-256:
`4b99169565af75a3e637a2cba19c1a3f59ab770444239931c969f94ab218357e`.
PostgreSQL HTML SHA-256:
`dd5402825098a9b4d8a05b2e0c13294c08f2b5753aef3c77081bffe30aadf2e4`.

In both snapshots the new normalized text is an exact suffix of the old text.
SQLite lost its preceding slogan/menu/search controls; PostgreSQL lost its
release banner and quick links. The remaining bytes from the page heading onward
were unchanged. SQLite's in-content executive summary still remains. These checks
support the narrow observed improvement; fewer bytes alone would not prove quality.

## Live repeat and remaining cases

The unchanged `config/pilot-five-hosts.json` was repeated in Release. It delivered
20 English pages from the same five hosts and 22 targets, stopping at `page_limit`.
The RFC redirect duplicate and German rejection remained; fetch failures, skipped
responses, empty texts and unknown languages remained zero, with 289 pending URLs.
All 20 URLs and URL-based page IDs matched the baseline.

Sources became 9 `main_role`, 1 `main`, 6 `article`, 2 `heading` and 2 `body`.
SQLite About and PostgreSQL About changed to `heading`. Seventeen text hashes
matched the previous live run; three changed, including Python's home page whose
source remained `main_role`. Live content changed between requests, so the frozen
comparison above, not all live hash changes, measures this extraction-rule change.

The two remaining body cases were inspected:

- SQLite's uses page contains multiple h1 sections, so the unique-heading guard
  deliberately retains body fallback.
- PostgreSQL's documentation directory wraps its h1 in a title-only `section`;
  useful content is outside that section. Selecting that wrapper would lose the
  directory, so it retains body fallback.

Host distribution remains 13/1/2/2/2: FIFO balancing is separate work. This change
does not complete boilerplate removal or the final 500–1000-page corpus.

The producing stem is `data/five-hosts-heading-20261113-214020`, with corpus,
journal and manifest suffixes matching the prior pilot. The corpus has 169019
bytes and SHA-256
`4b6d9a0d5b9a5249419583eb3043177e87b9a9f73d7ccf58ce048d6c5487b3df`.
The 6974-byte journal hash remains
`178b55826a35e0d050768c0d2d7d18f8f77649329be35e258f4be286cdc625fc`.
`verify_run.py` accepted artifacts and accounting. The local independent audit
also recomputed all per-page IDs/hashes and checked uniqueness, scope, versions,
language, timestamps and delivered-event correspondence. Raw artifacts remain local.

All 152 CTest entries passed in Debug and Release on MinGW GCC. New scenarios
cover selection bounds, normalization, skipped headings, ambiguity, semantic
priority, short/UTF-8 thresholds, deep trees, JSONL source/version and CLI link
discovery outside the selected region.
