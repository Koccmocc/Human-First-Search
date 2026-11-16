"""Verify saved crawler artifacts independently, using only the Python standard library."""

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import sys
from urllib.parse import urlsplit


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


class PageAudit:
    """Check current CLI page identities and scope without loading corpus texts into an index."""

    def __init__(self, manifest):
        config = manifest.get("configuration")
        processing = manifest.get("processing")
        require(isinstance(config, dict) and isinstance(processing, dict),
                "Page audit requires configuration and processing objects")
        hosts = config.get("allowed_hosts")
        require(isinstance(hosts, list) and hosts and all(isinstance(host, str) and host for host in hosts),
                "Page audit requires nonempty allowed_hosts")
        self.hosts = set(hosts)
        self.policy = config.get("language")
        require(self.policy in ("en", "any"), "Page audit requires language en or any")
        self.max_depth = nonnegative_integer(config.get("max_depth"), "configuration.max_depth")
        self.versions = {}
        for field, key in (("extractor_version", "extractor"), ("url_normalization_version", "url_normalization"),
                           ("language_detection_version", "language_detection")):
            value = processing.get(key)
            require(isinstance(value, str) and value, f"Page audit requires processing.{key}")
            self.versions[field] = value
        self.ids = set()
        self.hashes = set()
        self.by_host = Counter()
        self.by_source = Counter()
        self.characters = 0

    def url_host(self, url):
        require(isinstance(url, str) and url, "Expected a nonempty URL string")
        _ = url.encode("utf-8")  # JSON escapes can represent unpaired surrogates; real URLs cannot.
        require(not any(ord(char) <= 0x20 or ord(char) == 0x7f for char in url) and
                "\\" not in url and "#" not in url, "URL contains controls, whitespace, backslash or fragment")
        parsed = urlsplit(url)
        require(parsed.scheme in ("http", "https") and parsed.hostname is not None and
                parsed.username is None and parsed.password is None, "Expected a credential-free absolute HTTP(S) URL")
        _ = parsed.port  # Force validation of malformed or out-of-range ports.
        host = parsed.hostname
        if ":" in host:
            host = "[" + host + "]"  # Lexbor/config serialize IPv6 hosts with brackets.
        require(host in self.hosts, f"URL host is outside allowed_hosts: {host}")
        return host

    def check(self, page):
        url = page.get("url")
        host = self.url_host(url)
        text = page.get("text")
        require(isinstance(text, str) and text, "Expected nonempty extracted text")
        page_id = "sha256:" + hashlib.sha256(url.encode("utf-8")).hexdigest()
        text_hash = "sha256:" + hashlib.sha256(text.encode("utf-8")).hexdigest()
        require(page.get("page_id") == page_id, "page_id does not match exact URL bytes")
        require(page.get("text_hash") == text_hash, "text_hash does not match exact text bytes")
        require(page_id not in self.ids, "Duplicate page_id/URL in corpus")
        require(text_hash not in self.hashes, "Duplicate text_hash in corpus")
        for field, value in self.versions.items():
            require(page.get(field) == value, f"{field} differs from manifest processing version")
        source = page.get("text_source")
        require(source in ("main", "article", "main_role", "heading", "body"), "Unknown text_source")
        require(type(page.get("http_status")) is int and page["http_status"] == 200, "Expected http_status 200")
        depth = nonnegative_integer(page.get("depth"), "page.depth")
        require(depth <= self.max_depth, "Page depth exceeds configured maximum")
        chain = page.get("requested_urls")
        require(isinstance(chain, list) and chain, "Expected a nonempty requested_urls chain")
        for requested in chain:
            self.url_host(requested)
        require(chain[-1] == url, "requested_urls does not end at the page URL")
        require("language" in page and "language_percent" in page, "Missing language metadata")
        code, percent = page["language"], page["language_percent"]
        if code is None:
            require(self.policy == "any" and percent is None, "Unknown language is inconsistent with admission policy")
        else:
            require(isinstance(code, str) and code and type(percent) is int and 1 <= percent <= 100,
                    "Invalid detected language metadata")
            require(self.policy != "en" or code == "en", "Non-English page in English-only corpus")
        self.ids.add(page_id)
        self.hashes.add(text_hash)
        self.by_host[host] += 1
        self.by_source[source] += 1
        self.characters += len(text)

    def report(self):
        return {"checked_pages": len(self.ids), "pages_by_host": dict(sorted(self.by_host.items())),
                "text_sources": dict(sorted(self.by_source.items())), "text_characters": self.characters}


def verify_artifact(path, descriptor, kind, summary, page_audit=None):
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
            if kind == "corpus" and page_audit is not None:
                try:
                    page_audit.check(record)
                except ValueError as error:
                    raise ValueError(f"{context}: {error}") from error
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


def verify_run(manifest_path, corpus_path=None, log_path=None, *, check_pages=False,
               min_pages=None, min_hosts=None):
    requirements = {key: value for key, value in (("min_pages", min_pages), ("min_hosts", min_hosts))
                    if value is not None}
    for key, value in requirements.items():
        require(type(value) is int and value > 0, f"{key}: expected a positive integer")
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
    admission_fields = ["admission_" + outcome for outcome in
                        ("added", "duplicate", "invalid_url", "out_of_scope", "depth_limit", "queue_full", "url_limit")]
    # Older schema-1 manifests omit this whole group; missing counters are unknown, not zero.
    host_field = "admission_host_queue_full"
    if any(key in summary for key in admission_fields) or host_field in summary:
        if host_field in summary:
            admission_fields.append(host_field)
        for key in admission_fields:
            nonnegative_integer(summary.get(key), "summary." + key)
        require(summary["admission_added"] == summary["targets"] + summary["pending_urls"],
                "Admission added count differs from targets plus pending URLs")
        require(summary["rejected_seeds"] <= sum(summary[key] for key in admission_fields[1:]),
                "Rejected seed count exceeds all rejected admission attempts")
    artifacts = manifest.get("artifacts")
    require(isinstance(artifacts, dict), "Manifest has no artifacts object")
    page_audit = None
    if check_pages or requirements:
        require(artifacts.get("corpus") is not None, "Page audit requires a saved corpus")
        page_audit = PageAudit(manifest)
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
        verified[kind] = verify_artifact(path, descriptor, kind, summary, page_audit)
    require(verified, "Manifest has no saved corpus or journal to verify")
    report = {"verified": True, "artifacts": verified}
    if page_audit is not None:
        report["page_audit"] = page_audit.report()
    if requirements:
        # Count verified delivered pages/hosts, not the requested limit or allowlist size.
        pages, hosts = len(page_audit.ids), len(page_audit.by_host)
        require(min_pages is None or pages >= min_pages,
                f"Corpus has {pages} pages; requires at least {min_pages}")
        require(min_hosts is None or hosts >= min_hosts,
                f"Corpus has {hosts} hosts; requires at least {min_hosts}")
        report["requirements"] = requirements
    return report


def positive_integer(raw):
    try:
        value = int(raw)
    except ValueError:
        raise argparse.ArgumentTypeError("Expected a positive integer") from None
    if value <= 0:
        raise argparse.ArgumentTypeError("Expected a positive integer")
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--corpus", type=Path, help="Location of a copied corpus; recorded hash still applies")
    parser.add_argument("--log", type=Path, help="Location of a copied journal; recorded hash still applies")
    parser.add_argument("--check-pages", action="store_true", help="Also audit page hashes, uniqueness, scope and metadata")
    parser.add_argument("--min-pages", type=positive_integer, help="Require at least this many audited pages; enables page audit")
    parser.add_argument("--min-hosts", type=positive_integer, help="Require at least this many delivered hosts; enables page audit")
    arguments = parser.parse_args()
    try:
        report = verify_run(arguments.manifest, arguments.corpus, arguments.log, check_pages=arguments.check_pages,
                            min_pages=arguments.min_pages, min_hosts=arguments.min_hosts)
    except (OSError, ValueError, RecursionError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
