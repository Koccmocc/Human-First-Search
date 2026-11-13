"""Verify saved crawler artifacts independently, using only the Python standard library."""

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import sys


OUTCOME_COUNTERS = {
    "page_delivered": "pages",
    "fetch_failed": "fetch_failures",
    "response_skipped": "skipped_responses",
    "empty_text": "empty_texts",
    "duplicate_url": "duplicate_pages",
    "duplicate_text": "duplicate_texts",
    "non_english": "non_english_pages",
    "unknown_language": "unknown_language_pages",
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def reject_constant(value):
    raise ValueError(f"Non-JSON numeric constant: {value}")


def read_object(raw, context):
    try:
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=unique_object,
                           parse_constant=reject_constant)
        require(isinstance(value, dict), "Expected a JSON object")
        require(type(value.get("schema_version")) is int and value["schema_version"] == 1,
                "Expected integer schema_version 1")
        return value
    except ValueError as error:
        raise ValueError(f"{context}: {error}") from error


def nonnegative_integer(value, context):
    require(type(value) is int and value >= 0, f"{context}: expected a nonnegative integer")
    return value


def verify_artifact(path, descriptor, kind, summary):
    require(isinstance(descriptor, dict), f"{kind}: expected an artifact object")
    expected_bytes = nonnegative_integer(descriptor.get("bytes"), f"{kind}.bytes")
    expected_hash = descriptor.get("sha256")
    require(isinstance(expected_hash, str) and re.fullmatch(r"sha256:[0-9a-f]{64}", expected_hash),
            f"{kind}: invalid SHA-256 fingerprint")
    digest = hashlib.sha256()
    byte_count = records = 0
    outcomes = Counter()
    # Hash and parse the same bytes from one read-only handle; never load the whole file.
    with path.open("rb") as stream:
        for records, raw in enumerate(stream, start=1):
            context = f"{path}:{records}"
            require(raw.endswith(b"\n") and b"\r" not in raw, f"{context}: expected LF-terminated JSONL")
            digest.update(raw)
            byte_count += len(raw)
            record = read_object(raw, context)
            if kind == "journal":
                require(record.get("event") == "crawl_target", f"{context}: expected crawl_target event")
                outcome = record.get("outcome")
                require(isinstance(outcome, str) and outcome in OUTCOME_COUNTERS,
                        f"{context}: unknown crawl outcome")
                outcomes[outcome] += 1
    actual_hash = "sha256:" + digest.hexdigest()
    require(byte_count == expected_bytes, f"{kind}: byte count differs from manifest")
    require(actual_hash == expected_hash, f"{kind}: SHA-256 differs from manifest")
    counter = "pages" if kind == "corpus" else "targets"
    require(records == summary[counter], f"{kind}: record count differs from summary.{counter}")
    if kind == "journal":
        for outcome, counter in OUTCOME_COUNTERS.items():
            require(outcomes[outcome] == summary[counter],
                    f"journal: {outcome} count differs from summary.{counter}")
    return {"records": records, "bytes": byte_count, "sha256": actual_hash}


def verify_run(manifest_path, corpus_path=None, log_path=None):
    manifest = read_object(Path(manifest_path).read_bytes(), str(manifest_path))
    require(manifest.get("completed") is True, "Manifest does not mark a completed run")
    summary = manifest.get("summary")
    require(isinstance(summary, dict), "Manifest has no summary object")
    for key in (*OUTCOME_COUNTERS.values(), "targets", "rejected_seeds", "pending_urls"):
        nonnegative_integer(summary.get(key), f"summary.{key}")
    require(summary.get("stop") in ("queue_empty", "page_limit", "target_limit", "duration_limit"),
            "Unknown summary stop reason")
    require(sum(summary[key] for key in OUTCOME_COUNTERS.values()) == summary["targets"],
            "Summary outcome counters do not add up to targets")
    artifacts = manifest.get("artifacts")
    require(isinstance(artifacts, dict), "Manifest has no artifacts object")
    verified = {}
    for kind, override in (("corpus", corpus_path), ("journal", log_path)):
        require(kind in artifacts, f"Manifest is missing artifacts.{kind}")
        descriptor = artifacts[kind]
        if descriptor is None:
            require(override is None, f"{kind}: cannot override an artifact absent from the manifest")
            continue
        require(isinstance(descriptor, dict), f"{kind}: expected an artifact object")
        saved_path = descriptor.get("path")
        require(isinstance(saved_path, str) and saved_path, f"{kind}: missing artifact path")
        path = Path(override) if override is not None else Path(saved_path)
        require(override is not None or path.is_absolute(),
                f"{kind}: recorded path is not absolute on this OS; supply a path override")
        verified[kind] = verify_artifact(path, descriptor, kind, summary)
    require(verified, "Manifest has no saved corpus or journal to verify")
    return {"verified": True, "artifacts": verified}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--corpus", type=Path, help="Location of a copied corpus; recorded hash still applies")
    parser.add_argument("--log", type=Path, help="Location of a copied journal; recorded hash still applies")
    arguments = parser.parse_args()
    try:
        report = verify_run(arguments.manifest, arguments.corpus, arguments.log)
    except (OSError, ValueError, RecursionError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
