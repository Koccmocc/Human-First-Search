# H2 fallback and indexer snapshot: semantic-text-v4

Review of the 21 body-fallback pages in the 100-page v3 sample found SQLite pages
whose main page heading is `h2`, without an `h1`. Their text began with the site's
menu/search prelude. Version v4 extends the existing heading branch to this case.

## Bounded rule

Main/article/role-main priority is unchanged. Heading selection still prefers a
single HTML h1 outside skipped subtrees. Only when no h1 exists can a unique h2
be selected. An ambiguous, empty or unusable h1 does not fall through to h2.
Multiple h2s also keep body fallback. Existing parent (`body`/`div`/`section`),
nonempty-heading and 200 normalized UTF-8-byte suffix guards remain.

This keeps the `heading` source label, while the rule version becomes
`semantic-text-v4`. URL normalization and page-ID rules are unchanged. New text
can change `text_hash`, so an indexer can update content under the same page ID.
Basic `body_text()`, title extraction and all-document link discovery retain
their behavior. No host/class/ID selector is added.

## Controlled HTML comparison

All 21 previously delivered body-fallback URLs were saved as local HTML snapshots.
Both v3 and v4 processed those identical bytes. Seven changed from body to heading;
fourteen were unchanged. Every changed result was an exact suffix of the old
normalized text. The removed prefixes were inspected as menu/search/navigation
text; text from the selected heading onward was retained byte for byte.

| SQLite path | v3 text bytes | v4 text bytes |
| --- | ---: | ---: |
| `/docs.html` | 17198 | 16610 |
| `/download.html` | 6800 | 6604 |
| `/onefile.html` | 1576 | 1380 |
| `/limits.html` | 15224 | 15028 |
| `/zeroconf.html` | 885 | 689 |
| `/transactional.html` | 1095 | 899 |
| `/doclist.html` | 6049 | 5853 |

Snapshots, HTML fingerprints and exact comparison outputs remain under ignored
`build/body-review-v4/`. This demonstrates a narrow observed improvement, not
general extraction accuracy. The SQLite documentation page still contains search
controls inside its selected region. Other body fallbacks, title wrappers and
multi-heading documents can retain boilerplate, and navigation/archive/directory
content remains possible even with semantic selection.

Three new GoogleTest cases cover unique h2 selection, h1 precedence and unusable
h1/ambiguous h2/filtering/content guards. The CLI fixture verifies exported text,
source/version and discovery of a link outside the selected range. All 168 CTest
entries passed in Debug and Release on MinGW GCC.

## Fresh handoff run

The 2026-11-16 v4 run used `config/pilot-100-host-cap.json` and reached its
100-page limit in 117 targets, in 134711 ms. The read-only verifier passed both
artifact fingerprint and page-content checks. Distribution was Python 24,
RFC Editor 22, SQLite 23, PostgreSQL 9 and GNU 22. Sources were `main_role` 29,
`main` 22, `heading` 17, `article` 17 and `body` 15.

The journal records 16 fetch failures (13 timeouts, two request-policy denials
and one body-size limit) plus one unknown-language rejection. These failures
explain why the live sample differs from the preceding v3 run; this is not a
complete or equally represented crawl of those sites. Of 89 URLs shared by both
runs, all page IDs stayed stable, ten text hashes changed and seven source labels
changed from body to heading. Only the identical-HTML comparison above isolates
the extractor change from changes on live sites.

Corpus: 1332221 bytes, SHA-256
`fca5bf551b23c67a55c32fcd36cfc7401fadeb993b1d1de5d8ba159e30bcd487`.
Journal: 37801 bytes, SHA-256
`8cd75f3116880994ef25d6ab6b245bfc1cf882968cdfe83d9b2c0503b696a333`.
The producing manifest is kept locally at
`data/indexer-v4-20261116T164822Z-run.json`; raw data is not tracked in Git.

The [indexer handoff guide](indexer-handoff.md) defines how to consume the v4
snapshot and preserve identity/version metadata. The actual indexer still needs
to pass its own ingestion/update checks.
