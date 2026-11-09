"""Exercise the real CLI under http_fixture.py's loopback server."""

import os
import subprocess
import sys
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


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
