"""Exercise the real CLI under http_fixture.py's loopback server."""

import os
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

CRAWLER = sys.argv[1]
BASE = os.environ["HFS_TEST_BASE_URL"]


class CliCrawl(unittest.TestCase):
    def run_crawl(self, *arguments):
        return subprocess.run(
            [CRAWLER, "crawl", "--allow-host", "127.0.0.1", "--delay-ms", "0", *arguments],
            capture_output=True, text=True, encoding="utf-8", timeout=5,
        )

    def summary(self, result):
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(result.stderr.startswith("crawl: "), result.stderr)
        return dict(item.split("=", 1) for item in result.stderr.removeprefix("crawl: ").split())

    def fetch_time(self, record):
        self.assertRegex(record["fetched_at"], r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$")
        return datetime.strptime(record["fetched_at"], "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)

    def test_discovers_pages_and_keeps_summary_out_of_stdout(self):
        result = self.run_crawl("--seed", BASE + "/crawl/start", "--max-depth", "1")
        summary = self.summary(result)
        self.assertEqual(result.stdout.splitlines(), [BASE + path for path in (
            "/crawl/start", "/crawl/a", "/crawl/b")])
        self.assertEqual(summary["stop"], "queue_empty")
        self.assertEqual(summary["pages"], "3")
        self.assertEqual(summary["targets"], "4")
        self.assertEqual(summary["fetch_failures"], "1")

    def test_page_limit_stops_before_loading_children(self):
        result = self.run_crawl("--seed", BASE + "/crawl/start", "--max-pages", "1")
        summary = self.summary(result)
        self.assertEqual(result.stdout.splitlines(), [BASE + "/crawl/start"])
        self.assertEqual(summary["stop"], "page_limit")
        self.assertEqual(summary["pending_urls"], "3")

    def test_invalid_scope_fails_before_any_seed_request(self):
        result = self.run_crawl("--seed", BASE + "/must-not-follow",
                                "--seed", "http://example.invalid/outside")
        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, "")
        self.assertIn("Seed host is not allowed", result.stderr)

    def test_failed_page_is_a_completed_session_with_failure_statistics(self):
        result = self.run_crawl("--seed", BASE + "/private/page")
        summary = self.summary(result)
        self.assertEqual(result.stdout, "")
        self.assertEqual(summary["pages"], "0")
        self.assertEqual(summary["fetch_failures"], "1")

    def test_jsonl_round_trip_preserves_unicode_controls_hashes_and_redirect_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "corpus with spaces.jsonl"
            earliest = datetime.now(timezone.utc).replace(microsecond=0)
            result = self.run_crawl("--seed", BASE + "/crawl/export-alias", "--output", str(path))
            latest = datetime.now(timezone.utc)
            self.summary(result)
            raw = path.read_bytes()
            self.assertFalse(raw.startswith(b"\xef\xbb\xbf"))
            self.assertEqual(raw.count(b"\n"), 1)
            self.assertNotIn(b"\r", raw)
            self.assertTrue(raw.endswith(b"\n"))
            record = json.loads(raw.decode("utf-8"))
            expected_text = 'First "line" \\ path\nSecond caf\u00e9 \x01'
            self.assertEqual(record["url"], BASE + "/crawl/export")
            self.assertEqual(record["title"], 'Export "title" \\ caf\u00e9')
            self.assertEqual(record["text"], expected_text)
            self.assertEqual(record["schema_version"], 1)
            self.assertEqual(record["extractor_version"], "semantic-text-v1")
            self.assertEqual(record["text_source"], "body")
            self.assertEqual(record["url_normalization_version"], "lexbor-url-v1")
            self.assertEqual(record["requested_urls"], [BASE + "/crawl/export-alias", record["url"]])
            self.assertEqual(record["page_id"], "sha256:" + hashlib.sha256(record["url"].encode()).hexdigest())
            self.assertEqual(record["text_hash"], "sha256:" + hashlib.sha256(expected_text.encode()).hexdigest())
            self.assertEqual(record["http_status"], 200)
            self.assertEqual(record["depth"], 0)
            self.assertIsNone(record["language"])
            captured = self.fetch_time(record)
            self.assertLessEqual(earliest, captured)
            self.assertLessEqual(captured, latest)
            second = Path(directory) / "second.jsonl"
            self.summary(self.run_crawl("--seed", BASE + "/crawl/export-alias", "--output", str(second)))
            repeated = json.loads(second.read_text(encoding="utf-8"))
            self.fetch_time(repeated)
            # Separate live fetches have their own timestamps; stable content stays equal.
            del repeated["fetched_at"]
            del record["fetched_at"]
            self.assertEqual(repeated, record)

    def test_jsonl_contains_one_record_per_page_and_empty_export_has_no_records(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "pages.jsonl"
            result = self.run_crawl("--seed", BASE + "/crawl/start", "--max-depth", "1", "--output", str(path))
            self.summary(result)
            records = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
            self.assertEqual([record["url"] for record in records], result.stdout.splitlines())
            self.assertEqual(len(records), 3)
            empty = Path(directory) / "empty.jsonl"
            self.summary(self.run_crawl("--seed", BASE + "/private/page", "--output", str(empty)))
            self.assertEqual(empty.read_bytes(), b"")

    def test_jsonl_deduplicates_text_and_keeps_discovery_from_duplicate_pages(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "unique.jsonl"
            result = self.run_crawl("--seed", BASE + "/dedup/z", "--seed", BASE + "/dedup/a-alias",
                                    "--seed", BASE + "/dedup/a", "--output", str(path))
            summary = self.summary(result)
            records = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
            urls = [record["url"] for record in records]
            self.assertEqual(urls, [BASE + path for path in ("/dedup/z", "/dedup/from-z", "/dedup/from-a")])
            self.assertEqual(urls, result.stdout.splitlines())
            self.assertEqual(records[0]["title"], "First title")
            self.assertEqual(records[0]["text"], "Same & body\nNext")
            self.assertEqual(len({record["text_hash"] for record in records}), 3)
            for record in records:
                self.assertEqual(record["text_hash"], "sha256:" + hashlib.sha256(record["text"].encode()).hexdigest())
            self.assertEqual(summary["pages"], "3")
            self.assertEqual(summary["targets"], "5")
            self.assertEqual(summary["duplicate_texts"], "1")
            self.assertEqual(summary["duplicate_pages"], "1")

    def test_jsonl_uses_selected_content_for_hashes_and_preserves_external_discovery(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "semantic.jsonl"
            result = self.run_crawl("--seed", BASE + "/content/page", "--seed", BASE + "/content/copy",
                                    "--output", str(path))
            summary = self.summary(result)
            records = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
            self.assertEqual([record["url"] for record in records],
                             [BASE + path for path in ("/content/page", "/content/outside", "/content/copy-child")])
            expected_text = "Heading\nCaf\u00e9 & tea"
            self.assertEqual(records[0]["text"], expected_text)
            self.assertEqual(records[0]["extractor_version"], "semantic-text-v1")
            self.assertEqual([record["text_source"] for record in records], ["article", "main", "body"])
            self.assertEqual(records[0]["text_hash"], "sha256:" + hashlib.sha256(expected_text.encode()).hexdigest())
            self.assertEqual(records[0]["page_id"], "sha256:" + hashlib.sha256(records[0]["url"].encode()).hexdigest())
            self.assertEqual(summary["duplicate_texts"], "1")

    def test_existing_or_unavailable_output_fails_before_http_and_preserves_existing_data(self):
        with tempfile.TemporaryDirectory() as directory:
            existing = Path(directory) / "existing.jsonl"
            existing.write_bytes(b"keep this data\n")
            for path in (existing, Path(directory) / "missing" / "corpus.jsonl"):
                result = self.run_crawl("--seed", BASE + "/must-not-follow", "--output", str(path))
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertIn("JSONL", result.stderr)
            self.assertEqual(existing.read_bytes(), b"keep this data\n")


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
