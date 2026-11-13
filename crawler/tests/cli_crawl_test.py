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
    def run_crawl(self, *arguments, language="any"):
        # Mechanics fixtures are deliberately short; None exercises the CLI default.
        language_arguments = [] if language is None else ["--language", language]
        return subprocess.run(
            [CRAWLER, "crawl", "--allow-host", "127.0.0.1", "--delay-ms", "0",
             *language_arguments, *arguments],
            capture_output=True, text=True, encoding="utf-8", timeout=5,
        )

    def summary(self, result):
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(result.stderr.startswith("crawl: "), result.stderr)
        return dict(item.split("=", 1) for item in result.stderr.removeprefix("crawl: ").split())

    def run_config(self, path, *arguments):
        return subprocess.run(
            [CRAWLER, "crawl", "--config", str(path), *arguments],
            capture_output=True, text=True, encoding="utf-8", timeout=5,
        )

    def verify_saved_run(self, manifest):
        tool = Path(__file__).resolve().parents[2] / "tools" / "verify_run.py"
        result = subprocess.run([sys.executable, str(tool), str(manifest)],
                                capture_output=True, text=True, encoding="utf-8", timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def test_config_matches_explicit_options_and_writes_selected_destinations(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / "crawl config.json"
            values = {"schema_version": 1, "seeds": [BASE + "/language/german", BASE + "/language/short"],
                      "allowed_hosts": ["127.0.0.1"], "delay_ms": 0, "max_pages": 1, "language": "en"}
            config.write_text(json.dumps(values), encoding="utf-8")
            original = config.read_bytes()
            corpus = Path(directory) / "configured corpus.jsonl"
            log = Path(directory) / "configured log.jsonl"
            manifest = Path(directory) / "configured run.json"
            configured = self.run_config(config, "--output", str(corpus), "--log", str(log),
                                         "--manifest", str(manifest))
            explicit = self.run_crawl("--seed", BASE + "/language/german", "--seed", BASE + "/language/short",
                                      "--max-pages", "1", language="en")
            self.assertEqual(self.summary(configured), self.summary(explicit))
            self.assertEqual(configured.stdout, explicit.stdout)
            pages = [json.loads(line) for line in corpus.read_text(encoding="utf-8").splitlines()]
            events = [json.loads(line) for line in log.read_text(encoding="utf-8").splitlines()]
            self.assertEqual(len(pages), 1)
            self.assertEqual(pages[0]["language"], "en")
            self.assertEqual([page["url"] for page in pages], configured.stdout.splitlines())
            self.assertEqual(len(events), 3)
            self.assertEqual(config.read_bytes(), original)
            metadata = json.loads(manifest.read_bytes())
            self.assertEqual(metadata["configuration"]["seeds"], values["seeds"])
            self.assertEqual(metadata["configuration"]["language"], "en")
            self.assertEqual(metadata["summary"]["pages"], 1)
            self.verify_saved_run(manifest)

    def test_invalid_configs_fail_before_creating_outputs_or_requesting_seeds(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / "invalid.json"
            corpus = Path(directory) / "never-created.jsonl"
            log = Path(directory) / "never-created-log.jsonl"
            manifest = Path(directory) / "never-created-run.json"
            valid = {"schema_version": 1, "seeds": [BASE + "/must-not-follow"], "allowed_hosts": ["127.0.0.1"]}
            cases = ["broken", json.dumps(dict(valid, schema_version=2)), json.dumps(dict(valid, max_pages=0)),
                     json.dumps(dict(valid, allowed_hosts=["outside.invalid"])), json.dumps(dict(valid, language="de")),
                     json.dumps(dict(valid, max_pgaes=2)), '{"schema_version":1,' + json.dumps(valid)[1:]]
            for body in cases:
                with self.subTest(body=body):
                    config.write_text(body, encoding="utf-8")
                    result = self.run_config(config, "--output", str(corpus), "--log", str(log),
                                             "--manifest", str(manifest))
                    self.assertEqual(result.returncode, 2, result.stderr)
                    self.assertEqual(result.stdout, "")
                    self.assertFalse(corpus.exists())
                    self.assertFalse(log.exists())
                    self.assertFalse(manifest.exists())

    def test_config_file_failures_and_output_collision_preserve_config(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / "config.json"
            config.write_bytes(b" " * (64 * 1024 + 1))
            for path in (config, Path(directory) / "missing.json", Path(directory)):
                self.assertEqual(self.run_config(path).returncode, 2)
            config.write_text(json.dumps({"schema_version": 1, "seeds": [BASE + "/must-not-follow"],
                                         "allowed_hosts": ["127.0.0.1"]}), encoding="utf-8")
            original = config.read_bytes()
            result = self.run_config(config, "--output", str(config))
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertEqual(result.stdout, "")
            self.assertEqual(config.read_bytes(), original)

    def test_manifest_fingerprints_closed_multiblock_files_and_records_effective_options(self):
        with tempfile.TemporaryDirectory() as directory:
            corpus = Path(directory) / "large corpus.jsonl"
            log = Path(directory) / "journal.jsonl"
            manifest = Path(directory) / "run.json"
            before_ms = int(datetime.now(timezone.utc).timestamp() * 1000)
            result = self.run_crawl("--seed", BASE + "/manifest/large#discarded", "--max-pages", "1",
                                    "--max-depth", "0", "--output", str(corpus), "--log", str(log),
                                    "--manifest", str(manifest))
            after_ms = int(datetime.now(timezone.utc).timestamp() * 1000)
            summary = self.summary(result)
            record = json.loads(manifest.read_bytes())
            self.assertEqual(record["schema_version"], 1)
            self.assertIs(record["completed"], True)
            self.assertEqual(record["application"]["name"], "HumanFirstSearch")
            self.assertEqual(record["build"]["compiler"], "GNU")
            self.assertEqual(record["build"]["cplusplus"], 202002)
            self.assertTrue(record["build"]["compiler_version"])
            self.assertEqual(record["processing"], {"extractor": "semantic-text-v2",
                "url_normalization": "lexbor-url-v1", "language_detection": "cld2-compact-v1"})
            self.assertEqual(record["configuration"], {"schema_version": 1,
                "seeds": [BASE + "/manifest/large"], "allowed_hosts": ["127.0.0.1"],
                "max_pages": 1, "max_targets": 5000, "max_depth": 0, "max_pending": 1000,
                "max_urls": 5000, "max_duration_ms": 1800000, "delay_ms": 0, "language": "any"})
            self.assertEqual(record["summary"], {key: value if key == "stop" else int(value)
                                                for key, value in summary.items()})
            self.assertGreater(corpus.stat().st_size, 64 * 1024)
            self.assertEqual(self.verify_saved_run(manifest)["artifacts"]["corpus"]["records"], 1)
            for key, path in (("corpus", corpus), ("journal", log)):
                raw = path.read_bytes()
                self.assertEqual(record["artifacts"][key], {"path": path.resolve().as_posix(),
                    "bytes": len(raw), "sha256": "sha256:" + hashlib.sha256(raw).hexdigest()})
            timing = record["timing"]
            self.assertLessEqual(before_ms, timing["started_at_unix_ms"])
            self.assertLessEqual(timing["started_at_unix_ms"], timing["finished_at_unix_ms"])
            self.assertLessEqual(timing["finished_at_unix_ms"], after_ms)
            self.assertGreaterEqual(timing["elapsed_ms"], 0)
            # Replay just the settings, selecting fresh outputs independently.
            replay = Path(directory) / "replay.json"
            replay.write_text(json.dumps(record["configuration"]), encoding="utf-8")
            self.assertEqual(self.summary(self.run_config(replay)), summary)

    def test_manifest_handles_empty_corpus_and_absent_artifacts(self):
        with tempfile.TemporaryDirectory() as directory:
            corpus = Path(directory) / "empty.jsonl"
            manifest = Path(directory) / "empty-run.json"
            result = self.run_crawl("--seed", BASE + "/private/blocked", "--output", str(corpus),
                                    "--manifest", str(manifest))
            self.summary(result)
            record = json.loads(manifest.read_bytes())
            self.assertEqual(record["summary"]["pages"], 0)
            self.assertEqual(record["artifacts"]["corpus"]["bytes"], 0)
            self.assertEqual(record["artifacts"]["corpus"]["sha256"],
                             "sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")
            self.assertIsNone(record["artifacts"]["journal"])
            self.assertEqual(self.verify_saved_run(manifest)["artifacts"]["corpus"]["records"], 0)
            only = Path(directory) / "only-manifest.json"
            self.summary(self.run_crawl("--seed", BASE + "/language/english", "--max-depth", "0",
                                        "--manifest", str(only)))
            self.assertEqual(json.loads(only.read_bytes())["artifacts"], {"corpus": None, "journal": None})

    def test_manifest_destinations_fail_before_http_and_preserve_existing_files(self):
        with tempfile.TemporaryDirectory() as directory:
            existing = Path(directory) / "existing.json"
            existing.write_bytes(b"keep this manifest\n")
            for path in (existing, Path(directory), Path(directory) / "missing" / "run.json"):
                result = self.run_crawl("--seed", BASE + "/must-not-follow", "--manifest", str(path))
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertIn("Manifest", result.stderr)
            self.assertEqual(existing.read_bytes(), b"keep this manifest\n")
            for option in ("--output", "--log"):
                shared = Path(directory) / (option.removeprefix("--") + ".jsonl")
                alias = str(directory) + "/./" + shared.name
                result = self.run_crawl("--seed", BASE + "/must-not-follow", option, str(shared),
                                        "--manifest", alias)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertEqual(shared.read_bytes(), b"")

    def fetch_time(self, record):
        self.assertRegex(record["fetched_at"], r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$")
        return datetime.strptime(record["fetched_at"], "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)

    def test_default_english_admission_logs_rejections_and_discovers_their_children(self):
        with tempfile.TemporaryDirectory() as directory:
            corpus = Path(directory) / "english.jsonl"
            log = Path(directory) / "english-log.jsonl"
            result = self.run_crawl("--seed", BASE + "/language/german", "--seed", BASE + "/language/short",
                                    "--max-pages", "1", "--output", str(corpus), "--log", str(log), language=None)
            summary = self.summary(result)
            records = [json.loads(line) for line in corpus.read_text(encoding="utf-8").splitlines()]
            events = [json.loads(line) for line in log.read_text(encoding="utf-8").splitlines()]
            self.assertEqual(len(records), 1)
            page = records[0]
            self.assertEqual(page["url"], BASE + "/language/english")
            self.assertEqual(page["language"], "en")
            self.assertEqual(page["text_source"], "main_role")
            self.assertNotIn("Site controls", page["text"])
            self.assertGreaterEqual(page["language_percent"], 90)
            self.assertEqual(page["language_detection_version"], "cld2-compact-v1")
            self.assertEqual(page["text_hash"], "sha256:" + hashlib.sha256(page["text"].encode()).hexdigest())
            self.assertEqual(result.stdout.splitlines(), [page["url"]])
            self.assertEqual([event["outcome"] for event in events],
                             ["non_english", "unknown_language", "page_delivered"])
            self.assertEqual(events[0]["language"]["code"], "de")
            self.assertEqual(events[1]["language"]["status"], "too_short")
            self.assertIsNone(events[1]["language"]["code"])
            self.assertEqual(summary["non_english_pages"], "1")
            self.assertEqual(summary["unknown_language_pages"], "1")
            self.assertEqual(summary["pages"], "1")
            self.assertEqual(summary["targets"], "3")
            self.assertEqual(summary["pending_urls"], "1")
            self.assertEqual(summary["stop"], "page_limit")

            empty = Path(directory) / "rejected-only.jsonl"
            summary = self.summary(self.run_crawl("--seed", BASE + "/language/german",
                                                 "--seed", BASE + "/language/short", "--max-depth", "0",
                                                 "--output", str(empty), language=None))
            self.assertEqual(empty.read_bytes(), b"")
            self.assertEqual(summary["pages"], "0")
            self.assertEqual(summary["non_english_pages"], "1")
            self.assertEqual(summary["unknown_language_pages"], "1")

    def test_any_language_mode_exports_detection_without_rejection(self):
        with tempfile.TemporaryDirectory() as directory:
            corpus = Path(directory) / "any-language.jsonl"
            result = self.run_crawl("--seed", BASE + "/language/german", "--seed", BASE + "/language/short",
                                    "--max-depth", "0", "--output", str(corpus))
            summary = self.summary(result)
            records = [json.loads(line) for line in corpus.read_text(encoding="utf-8").splitlines()]
            self.assertEqual([record["language"] for record in records], ["de", None])
            self.assertGreater(records[0]["language_percent"], 0)
            self.assertIsNone(records[1]["language_percent"])
            for record in records:
                self.assertEqual(record["language_detection_version"], "cld2-compact-v1")
            self.assertEqual(summary["non_english_pages"], "0")
            self.assertEqual(summary["unknown_language_pages"], "0")

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

    def test_duration_limit_closes_exports_and_leaves_later_targets_unrequested(self):
        with tempfile.TemporaryDirectory() as directory:
            corpus = Path(directory) / "timed-corpus.jsonl"
            log = Path(directory) / "timed-log.jsonl"
            result = self.run_crawl("--seed", BASE + "/crawl/slow-start", "--seed", BASE + "/must-not-follow",
                                    "--max-duration-ms", "100", "--output", str(corpus), "--log", str(log))
            summary = self.summary(result)
            self.assertEqual(summary["stop"], "duration_limit")
            records = [json.loads(line) for line in corpus.read_text(encoding="utf-8").splitlines()]
            events = [json.loads(line) for line in log.read_text(encoding="utf-8").splitlines()]
            # Heavy scheduling delays may exhaust the budget even before the first target.
            self.assertIn(int(summary["targets"]), (0, 1))
            self.assertEqual(len(records), int(summary["pages"]))
            self.assertEqual(len(events), int(summary["targets"]))
            self.assertEqual([record["url"] for record in records], result.stdout.splitlines())
            if events:
                self.assertEqual(events[0]["url"], BASE + "/crawl/slow-start")
                self.assertEqual(events[0]["outcome"], "page_delivered")
                self.assertEqual(records[0]["text"], "Slow page")
                self.assertEqual(summary["pending_urls"], "1")
            else:
                self.assertEqual(summary["pending_urls"], "2")

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
            self.assertEqual(record["extractor_version"], "semantic-text-v2")
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
            self.assertEqual(records[0]["extractor_version"], "semantic-text-v2")
            self.assertEqual([record["text_source"] for record in records], ["article", "main", "body"])
            self.assertEqual(records[0]["text_hash"], "sha256:" + hashlib.sha256(expected_text.encode()).hexdigest())
            self.assertEqual(records[0]["page_id"], "sha256:" + hashlib.sha256(records[0]["url"].encode()).hexdigest())
            self.assertEqual(summary["duplicate_texts"], "1")

    def test_empty_text_is_excluded_from_jsonl_but_still_supplies_links(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "nonempty.jsonl"
            result = self.run_crawl("--seed", BASE + "/content/empty", "--max-pages", "1",
                                    "--output", str(path))
            summary = self.summary(result)
            records = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
            self.assertEqual([record["url"] for record in records], [BASE + "/content/outside"])
            self.assertEqual(result.stdout.splitlines(), [BASE + "/content/outside"])
            self.assertEqual(records[0]["text"], "Other page")
            self.assertEqual(summary["pages"], "1")
            self.assertEqual(summary["targets"], "2")
            self.assertEqual(summary["empty_texts"], "1")
            self.assertEqual(summary["duplicate_texts"], "0")

            empty = Path(directory) / "all-empty.jsonl"
            summary = self.summary(self.run_crawl("--seed", BASE + "/dedup/empty-one",
                                                 "--seed", BASE + "/dedup/empty-two",
                                                 "--output", str(empty)))
            self.assertEqual(empty.read_bytes(), b"")
            self.assertEqual(summary["pages"], "0")
            self.assertEqual(summary["empty_texts"], "2")
            self.assertEqual(summary["duplicate_texts"], "0")

    def test_log_round_trip_covers_outcomes_and_matches_summary_counts(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "crawl log.jsonl"
            corpus = Path(directory) / "corpus.jsonl"
            manifest = Path(directory) / "run.json"
            paths = ("/crawl/alias", "/crawl/nested/page", "/crawl/alias-two", "/dedup/z", "/dedup/a",
                     "/content/empty", "/crawl/error", "/encoding/broken", "/private/page", "/partial", "/loop-a")
            arguments = [item for path in paths for item in ("--seed", BASE + path)]
            result = self.run_crawl(*arguments, "--max-depth", "0", "--output", str(corpus), "--log", str(log),
                                    "--manifest", str(manifest))
            summary = self.summary(result)
            self.assertEqual(self.verify_saved_run(manifest)["artifacts"]["journal"]["records"], 11)
            raw = log.read_bytes()
            self.assertFalse(raw.startswith(b"\xef\xbb\xbf"))
            self.assertNotIn(b"\r", raw)
            self.assertTrue(raw.endswith(b"\n"))
            events = [json.loads(line) for line in raw.decode("utf-8").splitlines()]
            self.assertEqual(len(events), int(summary["targets"]))
            self.assertEqual([event["url"] for event in events], [BASE + path for path in paths])
            counters = {"page_delivered": "pages", "fetch_failed": "fetch_failures",
                        "response_skipped": "skipped_responses", "empty_text": "empty_texts",
                        "duplicate_url": "duplicate_pages", "duplicate_text": "duplicate_texts",
                        "non_english": "non_english_pages", "unknown_language": "unknown_language_pages"}
            for outcome, counter in counters.items():
                self.assertEqual(sum(event["outcome"] == outcome for event in events), int(summary[counter]))
            for event in events:
                self.assertEqual(event["schema_version"], 1)
                self.assertEqual(event["event"], "crawl_target")
                self.assertEqual(event["depth"], 0)
            self.assertEqual(events[0]["final_url"], BASE + "/crawl/nested/page")
            self.assertEqual(events[0]["http_status"], 200)
            self.assertEqual(events[0]["html_input"], "ready")
            self.assertIsNone(events[0]["error"])
            self.assertIsNone(events[1]["http_status"])
            self.assertEqual(events[2]["http_status"], 200)
            self.assertEqual(events[6]["http_status"], 404)
            self.assertIsNone(events[6]["html_input"])
            self.assertEqual(events[7]["html_input"], "invalid_text")
            for index, category, kind in ((8, "fetch", "request_denied"), (9, "http", "transport"),
                                          (10, "fetch", "redirect_loop")):
                self.assertIsNone(events[index]["final_url"])
                self.assertIsNone(events[index]["http_status"])
                self.assertEqual(events[index]["error"]["category"], category)
                self.assertEqual(events[index]["error"]["kind"], kind)
                self.assertTrue(events[index]["error"]["message"])
            records = [json.loads(line) for line in corpus.read_text(encoding="utf-8").splitlines()]
            self.assertEqual([record["url"] for record in records], result.stdout.splitlines())
            self.assertEqual([record["url"] for record in records],
                             [event["final_url"] for event in events if event["outcome"] == "page_delivered"])

    def test_log_without_corpus_excludes_pending_targets_at_page_limit(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "events.jsonl"
            result = self.run_crawl("--seed", BASE + "/crawl/start", "--max-pages", "1", "--log", str(log))
            summary = self.summary(result)
            events = [json.loads(line) for line in log.read_text(encoding="utf-8").splitlines()]
            self.assertEqual(len(events), 1)
            self.assertEqual(events[0]["outcome"], "page_delivered")
            self.assertEqual(result.stdout.splitlines(), [BASE + "/crawl/start"])
            self.assertEqual(summary["pending_urls"], "3")

    def test_log_rejects_existing_unavailable_and_shared_destinations_before_http(self):
        with tempfile.TemporaryDirectory() as directory:
            existing = Path(directory) / "existing.jsonl"
            existing.write_bytes(b"keep this log\n")
            for path in (existing, Path(directory), Path(directory) / "missing" / "log.jsonl"):
                result = self.run_crawl("--seed", BASE + "/must-not-follow", "--log", str(path))
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual(result.stdout, "")
                self.assertIn("JSONL", result.stderr)
            self.assertEqual(existing.read_bytes(), b"keep this log\n")
            shared = Path(directory) / "shared.jsonl"
            alias = str(directory) + "/./shared.jsonl"
            result = self.run_crawl("--seed", BASE + "/must-not-follow",
                                    "--output", str(shared), "--log", alias)
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertEqual(result.stdout, "")
            self.assertEqual(shared.read_bytes(), b"")

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
