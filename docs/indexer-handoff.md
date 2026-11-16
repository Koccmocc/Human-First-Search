# Crawler handoff to the indexer

This is the crawler producer's schema-1 contract and ingestion guide. The local
handoff archive contains a verified 100-page development snapshot. Final corpus
size/quality and acceptance by the real indexer remain separate integration work.

## Start with the saved snapshot

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

The bundled run contains 100 accepted English pages: Python 24, RFC Editor 22,
SQLite 23, PostgreSQL 9 and GNU 22. Its 117 target attempts also include 16 fetch
failures and one unknown-language rejection, recorded in the journal. The
[extraction review](h2-fallback.md#fresh-handoff-run) records fingerprints,
selection results and limitations. A successful audit checks the export's
consistency; it does not certify relevance or boilerplate-free text.

The crawler source is in the team's
[`crawler` branch](https://github.com/Grompesto/human-first-search/tree/crawler).
`SOURCE_COMMIT.txt` identifies the source revision used for this handoff. The
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
`semantic-text-v4`, and can change text/hash without changing URL identity.

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

Confirm that the actual indexer can stream all 100 records, store the full string
IDs, preserve URL/title/hash metadata, and find a known phrase from a selected
page. Repeat ingestion to check upsert behavior and test a changed text hash
under the same page ID. These checks require the real indexer and have not been
claimed as completed by crawler-only tests.

This development snapshot remains smaller than the planned 500–1000-page corpus.
Body fallback, in-page navigation and directory/archive/release-list content can
remain. Language admission is statistical, and page sources are quality-review
signals rather than quality guarantees. The accompanying extraction review
records the v4 improvement and residual cases. Transfer a completed, audited
snapshot as one unit; interrupted crawler output may be partial.
