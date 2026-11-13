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
