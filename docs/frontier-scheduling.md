# Frontier scheduling: depth-host-round-robin-v1

The five-host FIFO pilot delivered 13 of its 20 pages from Python.org. A long
list of links discovered early occupied the front of the global queue. The new
default scheduling policy alternates hosts among admitted targets at each depth.

## Ordering

`pop()` chooses the smallest pending depth, then the next host at that depth:

1. Each host/depth has its own FIFO of canonical URL targets.
2. Hosts join a depth's ring in order of their first successful admission there.
3. After one target is popped, a nonempty host queue moves to the ring's end.
4. An empty host is removed. If it receives more work later, it rejoins at the end.
5. When that depth empties, the next nonempty depth becomes eligible.

For seeds admitted as `A1, A2, A3, B1, B2, C1`, the pop order is
`A1, B1, C1, A2, B2, A3`. All admitted seeds still precede discovered children.
A single-host session keeps its previous breadth-first/FIFO behavior. URL order
within one host/depth follows seed or DOM link admission order. Host grouping
uses the canonical destination URL's hostname, sharing schemes and ports while
keeping separately allowed subdomains distinct.

The implementation stores ordered depth buckets, each holding a list of host
queues. List splicing rotates a nonempty host without allocating during `pop()`.
Empty depth/host buckets are removed. Admission rolls back if storage allocation
fails, preserving the URL budget and deduplication state.

## Scope and limits

This is fairness of queue-target attempts among currently admitted work at the
same depth. Failures, duplicates, response skips and language rejections consume
their host's turn just like delivered pages. Redirect chains remain one target;
the final response may belong to another allowed host, so final-host page counts
can differ from target-host scheduling counts. Robots, host delays, redirect
checks, language admission and URL/text deduplication keep their existing rules.

There is no per-host page quota, throughput guarantee or automatic retry. A
host without eligible targets cannot receive a turn. Global pending/total URL
capacities still apply across every depth and host; a large link list can still
fill admission capacity before later candidates are discovered. Rejected
candidates are not marked known and can be offered again, but the crawler does
not retain a retry backlog. This scheduling change does not solve fair admission
under queue saturation or guarantee equally sized source samples.

Visit order can change which text duplicate becomes the representative and which
pages fit the global limit. New manifests record `processing.frontier` as
`depth-host-round-robin-v1`; old manifests may lack this field. Configuration
schema and CLI flags are unchanged. Extraction, URL normalization and language
rule versions keep their values because those transformations did not change.

## Validation

Six additional GoogleTest frontier scenarios cover uneven queues, depth priority,
host reactivation, canonical hostname grouping, global capacities/retry eligibility
and assignment of cross-host links to their destination host. Existing single-host
FIFO, normalization, deduplication, depth and capacity tests remain in place.

A loopback HTTP scenario uses `127.0.0.1` and `localhost` as two allowed hosts.
One root supplies 12 links and the other supplies 3. With a six-page budget,
the crawl delivers the two roots followed by alternating children: three pages
per host, in the expected depth and event order. Manifest and CLI checks also
verify the recorded scheduling policy.

All 158 CTest entries passed in Debug and Release on MinGW GCC, with no new
build warnings. A pre-existing real-clock assertion exposed an 8 ms discrepancy
between Python and C++ wall-clock observations on Windows. Its process-level
bounds now allow one second of clock granularity/skew; deterministic C++ manifest
unit tests still check supplied timestamps exactly, and elapsed time remains
checked separately.

## Five-host live repeat

The same `config/pilot-five-hosts.json`, Release build, 20-page limit and one-second
host delay were used. Scope/robots/language rules and extraction v3 were retained.

| Host | Previous global FIFO | Depth/host round robin |
| --- | ---: | ---: |
| `www.python.org` | 13 | 5 |
| `www.rfc-editor.org` | 1 | 3 |
| `www.sqlite.org` | 2 | 5 |
| `www.postgresql.org` | 2 | 4 |
| `www.gnu.org` | 2 | 3 |

The largest delivered-host share fell from 65% to 25% in this sample. The new run
stopped at `page_limit` after 20 pages and 24 targets, leaving 287 pending URLs.
The journal recorded one duplicate URL, the known German rejection, one
`unknown_language` result (`too_short` on RFC Editor's search page), and one
`fetch_failed` with `request_denied` for the GNU search CGI target. That typed
failure combines caller scope/robots policy checks; it does not identify an HTTP
error. Skipped responses, empty texts and text duplicates were zero.

`verify_run.py` accepted artifact fingerprints and all accounting. The local audit
also recomputed every page ID and text hash, checked uniqueness, allowed request
chains, processing versions, English metadata, depth, UTC timestamps and matching
delivered journal events. New sources were 8 `main_role`, 3 `main`, 3 `heading`
and 6 `body`: broader host coverage exposes more pages needing content review.
This change does not improve extraction rules or complete the final corpus.

Local artifact stem: `data/five-hosts-balanced-20261113-220848`, with
`-corpus.jsonl`, `-log.jsonl` and `-run.json` suffixes. The run elapsed 23282 ms
before hashing/manifest writing. Files remain ignored locally.

| Artifact | Records | Bytes | SHA-256 |
| --- | ---: | ---: | --- |
| Corpus | 20 | 133120 | `4a42e2803b0ed22774bb67e93adeaf4ff636bb5714b8b2290f9a28165a04730d` |
| Journal | 24 | 7506 | `92475a0080d90cbe8206eb2c75eb00ea237ec848424ab4695d9be2b3c401342c` |

Live content and availability can change. These are observed results and local
artifact identities, not guaranteed counts or hashes for future runs.
