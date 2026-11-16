# Crawler handoff to the indexer

This is the crawler producer's schema-1 contract and ingestion guide. Handoff
archives contain audited saved snapshots. Final corpus size/quality and acceptance
by the real indexer remain separate integration work.

## Create a handoff archive

From the source checkout, after a successful crawl saved both corpus and journal:

```powershell
python tools/package_run.py data/indexer-v4-20261116T164822Z-run.json --output data/indexer-handoff.zip --source-commit b2a706069df65a4d91d5de6219d59ffd3c716053
```

Those example paths/revision refer to the local v4 pilot. For another run, supply
its own manifest and the full lowercase Git hash of the producing crawler code.
Do not substitute today's `HEAD` for the revision that produced an older snapshot.
The caller declares this revision: the manifest currently stores neither a Git
commit nor a binary hash, so the tool cannot prove that attribution.

The output parent must exist and the `.zip` destination must be fresh. Both saved
artifacts and the manifest are copied to a temporary workspace, then the copied
artifacts undergo `--check-pages` verification before ZIP creation. No sites are
requested. For moved artifacts, add `--corpus PATH --log PATH`; original hashes
still apply. Missing journals, damaged data and incomplete manifests are rejected.

The tool includes the manifest's effective configuration, an audit report,
`SOURCE_COMMIT.txt`, the verifier and an explicit list of public docs from the
packaging checkout. It never scans local data/build directories or personal notes
for additional contents. Existing destinations are not overwritten. Exit 0 prints
the new archive's path, byte count, SHA-256 and page count as JSON. Wait for that
successful exit before transferring the file; process/system interruption during
the final copy can leave an incomplete destination.

## Start with the saved snapshot

The latest local handoff is `data/indexer-handoff-filtered-500.zip`: 500 pages
from six delivering hosts under `url-export-v1`. See the
[filtered corpus report](filtered-corpus-2026-11-16.md) for provenance, hashes,
comparison and quality limits. Earlier unfiltered packages remain baseline samples.

Unpack the archive and run from its root with Python 3:

```powershell
python tools/verify_run.py manifest.json --corpus corpus.jsonl --log crawl-log.jsonl --check-pages
```

The archive includes `corpus.jsonl`, `crawl-log.jsonl`, `manifest.json`,
`audit-report.json`, `config.json`, `tools/verify_run.py`, `docs/` and
`SOURCE_COMMIT.txt`. Verification uses only Python's standard library, without
building C++ or fetching the sites. Both path overrides are intentional: the
original manifest retains the producing machine's absolute paths, while the
fingerprints apply to the exact copied bytes. Exit 0 means verification passed.

For a 500-page/five-host handoff, a current verifier can also enforce the minimums:

```powershell
python tools/verify_run.py manifest.json --corpus corpus.jsonl --log crawl-log.jsonl --min-pages 500 --min-hosts 5
```

These options enable page audit automatically and count actual delivered records
and final hostnames. A valid smaller snapshot is rejected for this handoff.
See the [minimum requirement rules](run-manifest.md#minimum-corpus-requirements).
Older archives may contain a verifier without these flags; use the current source
checkout's `tools/verify_run.py` against their unpacked manifest/corpus/journal,
or repack with current tools. The ordinary `--check-pages` command above remains
valid for those older archives.

The initial 2026-11-16 v4 snapshot contains 100 accepted English pages: Python 24,
RFC Editor 22, SQLite 23, PostgreSQL 9 and GNU 22. Its 117 target attempts also include 16 fetch
failures and one unknown-language rejection, recorded in the journal. The
[extraction review](h2-fallback.md#fresh-handoff-run) records fingerprints,
selection results and limitations. A successful audit checks the export's
consistency; it does not certify relevance or boilerplate-free text.

Other archives can have different counts/versions; inspect their manifest and
audit report. Historical reports in `docs/` describe their own runs.

The subsequent [500-page snapshot](pilot-500-pages-2026-11-16.md) is the larger
local handoff at `data/indexer-handoff-500.zip`. It contains 500 accepted English
pages across Kernel, PostgreSQL, Python, RFC Editor and SQLite. It meets the
lower planned volume; the report records sampled quality issues and the earlier
DNS-related shortfall. Verify and read the archive using the same commands above.

The crawler source is in the team's
[`crawler` branch](https://github.com/Grompesto/human-first-search/tree/crawler).
`SOURCE_COMMIT.txt` records the caller-declared producing revision. The
repository README documents C++20/GCC/CMake/Ninja builds. Windows MinGW builds
are verified; other native crawler toolchains/OS builds remain unverified here.

## Input contract

`corpus.jsonl` is UTF-8, one JSON object per LF-terminated line, without a BOM or
array wrapper. Strings contain JSON escapes for newlines/control characters.
Use a JSON parser rather than splitting text fields by delimiters. Full field
definitions are in [corpus-format.md](corpus-format.md).

| Indexer use | Crawler field | Contract |
| --- | --- | --- |
| Document identity/upsert key | `page_id` | String: `sha256:` plus 64 lowercase hex digits; hash of exact canonical URL UTF-8 bytes |
| Embedding/search content | `text` | Nonempty extracted plain-text string; can contain newlines and code/list text |
| Content change marker | `text_hash` | SHA-256 of exact `text` UTF-8 bytes, with the same prefix/hex format |
| Result link | `url` | Canonical final credential-free HTTP(S) URL |
| Display title | `title` | String, possibly empty |
| Processing provenance | `extractor_version`, `url_normalization_version`, `language_detection_version` | Preserve alongside the indexed document |
| Review/provenance metadata | `text_source`, `language`, `language_percent`, `fetched_at`, `depth` | Preserve as metadata rather than interpreting them as relevance scores |

Schema version is integer `1`. Required field names and types are the producer
contract within that schema; incompatible changes require a schema bump.
Additional metadata fields may be added, so consumers should tolerate unknown
keys. Extraction-rule changes use `extractor_version`, currently
`semantic-text-v5`, and can change text/hash without changing URL identity.
V5 adds the `title_container` source label; store source labels as versioned
provenance metadata. Use a current verifier for new exports. The saved filtered
500-page archive described above retains its original v4 records and source
revision; [the v5 review](title-container-fallback.md) uses controlled HTML snapshots.

## A minimal streaming adapter

This returns ordinary mappings; connect them to the indexer's chosen API:

```python
import json

def read_pages(path):
    with open(path, encoding="utf-8") as corpus:
        for line in corpus:
            page = json.loads(line)
            if page.get("schema_version") != 1:
                raise ValueError("Unsupported corpus schema")
            yield {
                "id": page["page_id"],
                "text": page["text"],
                "metadata": {key: page.get(key) for key in (
                    "url", "title", "text_hash", "extractor_version",
                    "url_normalization_version", "language_detection_version",
                    "text_source", "language", "language_percent", "fetched_at")},
            }

for document in read_pages("corpus.jsonl"):
    print(document["id"], document["metadata"]["url"])
```

Run the audit before ingestion. The adapter is intentionally small and does not
replace validation. `text` is already extracted plain text: a second HTML parser
would reinterpret literal code/examples. Do not silently recanonicalize URLs or
normalize text before verifying their stored hashes.

## Updates, duplicates and chunks

- Same `page_id`, changed `text_hash`: replace/reindex that page's prior content
  and associated chunks. A stable URL does not imply unchanged content.
- Same `page_id`, same `text_hash`: embeddings can be reused under the same
  indexer preprocessing/model configuration; metadata can still change.
- Each crawl emits one representative per extracted-text hash for that session.
  Merging multiple snapshots still needs the indexer's own upsert/duplicate policy.
- Snapshot absence does not prove deletion. A bounded crawl stops at page/target/
  time/queue limits and does not enumerate an entire site.
- The indexer chooses tokenization/chunking for its embedding model. Preserve
  parent `page_id` and `text_hash` with chunks so page updates replace the right
  content. Pages can be long; the crawler does not enforce an embedding token limit.

The crawler supplies no embeddings, AI-authorship score, search ranking or index
storage format. `language_percent` is estimated dominant-language text share,
not a confidence probability, relevance score or evidence of human authorship.

## Acceptance checks and current limits

Confirm that the actual indexer can stream every record counted by the audit,
store the full string IDs, preserve URL/title/hash metadata, and find a known phrase from a selected
page. Repeat ingestion to check upsert behavior and test a changed text hash
under the same page ID. These checks require the real indexer and have not been
claimed as completed by crawler-only tests.

The newer 500-page snapshot reaches the lower planned 500–1000-page corpus size.
Body fallback, in-page navigation and directory/archive/release-list content can
remain. Language admission is statistical, and page sources are quality-review
signals rather than quality guarantees. The accompanying extraction review
records the v4 improvement and residual cases. Transfer a completed, audited
snapshot as one unit; interrupted crawler output may be partial.
