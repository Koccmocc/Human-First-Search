# Frontier admission counters

The 100-page pilot reached its 1000-URL pending capacity. Scheduling was balanced,
but the old summary could not show how many link candidates were rejected at
admission. `AdmissionCounts` now records the result of every completed
`UrlFrontier::add_seed()` and `add_link()` call.

## Meaning and priority

Counts combine seed and discovered-link attempts. They describe calls, not
unique rejected URLs: offering the same rejected URL three times records three
rejections. Pop operations, robots requests and redirect hops are not admission
calls. A call that throws has no completed outcome; the crawl aborts as before.

`UrlFrontier::admission_counts()` returns an owning snapshot. `crawl()` copies the
final snapshot into `CrawlSummary::admission` on normal return. CLI stderr and the
manifest's flat `summary` object expose matching integer fields:

| Field | Recorded result |
| --- | --- |
| `admission_added` | URL admitted to pending work and the session's known set |
| `admission_duplicate` | Canonical URL already known, including after it was popped |
| `admission_invalid_url` | URL module rejected the candidate, including unsupported schemes/credentials |
| `admission_out_of_scope` | Host is outside the explicit allowlist |
| `admission_depth_limit` | Parent already at/beyond maximum link depth |
| `admission_queue_full` | Pending-entry capacity reached |
| `admission_url_limit` | Total admitted-URL budget exhausted |

Each call records exactly one result under the existing checks. For links, depth
is checked before resolving the reference, so even an invalid reference can be
classified as depth-limited. Otherwise resolution precedes scope, duplicate,
total-URL and pending-capacity checks, in that order. If both capacities are
exhausted, the URL-budget reason wins. This observes existing behavior rather
than changing admission, ordering, retries or processing versions.

The sum of the seven counters is the number of completed admission attempts.
For a normally returned crawl:

```text
admission_added = targets + pending_urls
rejected_seeds <= sum(all six rejected-admission counters)
```

`rejected_seeds` remains a seed-only count. `duplicate_pages` remains a completed
queue-target outcome (typically a known redirect destination); it is distinct
from `admission_duplicate`. Rejected links do not become queue targets or journal
rows. Empty/duplicate/language-rejected valid HTML still supplies link attempts.

## Interpreting saturation

`admission_queue_full` measures pressure from repeated candidate offers. It does
not identify unique lost pages, record their hosts or prove that a rejected URL
was never retried/admitted later. Capacity rejections still leave a URL eligible
for another offer. The crawler does not retain a retry backlog.

Many duplicates, out-of-scope references and unsupported schemes can be normal
on real sites. These counters are separate from HTTP/fetch failures. A high
queue-full count can guide capacity/source-selection review, but does not by
itself establish that increasing the queue will improve the final corpus.

The schema version remains 1; the seven summary fields are additive. Earlier
manifests may omit the entire group, meaning unknown counts. `verify_run.py`
accepts that absence. If any admission field is present, all seven must be
nonnegative integers, and the two relations above must hold. Missing/partial
groups are not silently treated as zero.

## Validation

Two new GoogleTest frontier cases check all results, check priority and repeated
capacity rejection followed by successful retry. A loopback crawl checks seed
and link accounting under both capacity limits. CLI/manifest tests compare the
same counters, and the independent verifier rejects partial or inconsistent
groups while retaining older-manifest support.

All 160 CTest entries passed in Debug and Release on MinGW GCC without new build
warnings. The Python verifier now has 16 scenarios inside its existing CTest entry.

## 100-page live repeat

The unchanged `config/pilot-100-pages.json` was repeated in Release with new output
paths. It again delivered 100 pages from five hosts in a 21/19/21/20/19 distribution,
processed 104 targets and stopped at `page_limit`, leaving 1000 pending URLs.
All 100 delivered URLs and URL-based page IDs matched the preceding pilot.
Observed elapsed time was 65946 ms; this live repeat is not a performance benchmark.
The three fetch failures and one unknown-language result remained in the journal.
`verify_run.py --check-pages` accepted the artifacts and admission relations.

| Result | Completed attempts |
| --- | ---: |
| Added | 1104 |
| Duplicate | 8013 |
| Invalid URL | 272 |
| Out of scope | 2784 |
| Depth limit | 0 |
| Queue full | 8949 |
| Total URL limit | 0 |
| **Total** | **21122** |

This identifies pending-capacity pressure: 8949 offers were rejected as queue
full, while no call was classified as total-URL-budget exhaustion. The accepted
count reconciles exactly: 1104 = 104 processed + 1000 pending. The rejected counts
include repeated offers and cannot be converted into a count of unique lost
pages. The same rejected URL may also have been accepted on a later offer.

The producing stem is `data/pilot-100-admission-20261116T160543Z`, with corpus,
journal and manifest suffixes matching the initial pilot. Files remain ignored
locally. Corpus: 100 records, 1294870 bytes, SHA-256
`d43d8501d42b3b13c6a17d693570e291768b62dbe1e2392e3771b541a74b6066`.
Journal: 104 records, 34216 bytes, SHA-256
`9dbbaf185957a7a7d47d4900a5f5f73436586686bc367a04bae2d47eca2a7d19`.
These observations and hashes identify this run, not expected values for future
live content. The counters supply diagnostics; they do not fix queue saturation
or the remaining content-quality issues.
