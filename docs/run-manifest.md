# Crawl run manifest, schema version 1

Select a fresh destination with `--manifest PATH`, in direct CLI or config mode:

```powershell
./build/release/crawler/hfs-crawler.exe crawl --config config/pilot.json --output data/pilot-corpus.jsonl --log data/pilot-log.jsonl --manifest data/pilot-run.json
```

The parent directory must exist. Like corpus and journal destinations, the
manifest path is checked before opening; existing paths and aliases of earlier
outputs are rejected before requests. Paths resolve from the working directory.
The manifest is optional and may also be requested without either JSONL file.
It is not a key in the saved crawl configuration.

The CLI reserves the manifest file before crawling, then fills it after a normal
return, checked closure of corpus/journal, and successful flushing of stdout and
the stderr summary. The manifest itself is closed with error checking. Read it
only after the process exits `0`. This is not an atomic publication protocol:
fatal errors or interruption may leave an empty or partial manifest and partial
JSONL outputs. A summary may already be printed when manifest creation fails.
Keep exclusive ownership of output paths throughout the run and hashing.

The file contains one UTF-8 JSON object followed by LF:

| Field | Meaning |
| --- | --- |
| `schema_version` | Integer `1` |
| `completed` | `true` for a normally returned session, including limits or zero accepted pages |
| `application` | Crawler product name and project version |
| `build` | GNU compiler and CMake versions, build configuration, `__cplusplus` value |
| `dependencies` | Compiled curl, Lexbor, nlohmann/json versions and CLD2's model/version description |
| `processing` | Extractor, URL normalization and language detection rule versions |
| `configuration` | Canonical ordered seeds, hosts, and every effective schema-1 config option, including defaults |
| `additional_options` | Page/robots HTTP timeouts and body limits, robots cache TTL/retry interval, redirect limit |
| `timing` | Wall-clock start/finish as Unix milliseconds, elapsed milliseconds measured with a steady clock |
| `summary` | Stop reason and all counters printed by the CLI, with numeric counts |
| `artifacts` | `corpus` and `journal`, each either `null` or a fingerprint object |

Each artifact object has an absolute, lexically normalized `path` with `/`
separators, a `bytes` count, and `sha256` containing `sha256:` plus 64 lowercase
hex digits. SHA-256 covers every byte of the closed binary file, including JSON
escaping, metadata and LF endings. Hashing uses 64 KiB chunks, without loading
the whole corpus into memory. An empty corpus has zero bytes and the standard
empty-input SHA-256. Missing destinations have `null` artifacts; stdout is not
fingerprinted. The manifest does not hash itself.

Start time is observed immediately before calling `crawl()`. Finish and elapsed
time include result closure and summary flushing, before artifact hashing and
manifest writing. Wall clocks can be adjusted; elapsed time comes from a
separate steady clock. A successful run does not imply a particular page count
or no individual fetch failures: inspect `summary`.

Save just the `configuration` object as a new JSON file to replay the supported
settings with `--config`. `additional_options` describes library settings that
are not configurable through that schema. The complete manifest is not itself
a config file. Absolute artifact paths describe the producing machine and may
need remapping when sharing files.

This records processing versions and verifies artifact identity. It does not
freeze live web content, store raw responses, identify the source Git revision
or fingerprint the executable. Dependency revisions remain pinned in CMake.
A full corpus snapshot, quality review and integration checks remain separate
work before the final reproducibility claim.

GoogleTest covers config round trips, effective defaults, supplied clocks, stop
reasons, invalid UTF-8 and output failures. Loopback process tests independently
compare fingerprints with Python `hashlib`, including a corpus larger than the
64 KiB read buffer, empty files, absent outputs, config-mode propagation and
destination errors before HTTP. Existing identity tests retain known SHA-256
vectors for the shared backend.

## Verify saved artifacts

After a successful crawl, run the independent, read-only verifier:

```powershell
python tools/verify_run.py data/pilot-run.json
```

It uses only the Python 3 standard library. It reads the saved corpus and/or
journal from the manifest's absolute paths, hashes the exact bytes with
`hashlib`, and compares their sizes and SHA-256 fingerprints. JSONL is read one
line at a time, so memory depends on the largest record, not the total corpus.
The manifest itself is read into memory.

Every record must be a UTF-8 JSON object with integer `schema_version: 1` and
LF termination. BOM, CRLF, blank lines, malformed JSON, duplicate keys and
non-JSON constants such as `NaN` are rejected. Corpus records must match
`summary.pages`. Journal records must be `crawl_target` events with known
outcomes; their total must match `summary.targets`, and every outcome count must
match its summary counter. Outcome counters must also add up to targets.

For files copied to another directory or operating system, override their
locations while retaining the original sizes and hashes:

```powershell
python tools/verify_run.py shared/pilot-run.json --corpus shared/pilot-corpus.jsonl --log shared/pilot-log.jsonl
```

Override paths are relative to the working directory when not absolute. An
override for an artifact recorded as `null` is an error. A manifest with no saved
artifacts is also rejected because there are no files to verify. An empty saved
corpus remains valid when the recorded size, hash and page count agree.

Exit `0` prints one JSON report to stdout with `verified: true` and the checked
artifacts' record counts, byte counts and hashes. A missing/corrupt file or
inconsistent manifest exits `1`, with an error on stderr and no success report;
invalid command syntax exits `2`. No files are created or modified.

This checks integrity and accounting against the supplied manifest. It does not
validate every corpus field, recompute per-page IDs/text hashes, prove English
or extraction quality, or establish the manifest's authenticity. Keep files
unchanged while checking them. The verifier can check a normally finished run
with fetch failures or zero pages; it does not enforce the final corpus size.
