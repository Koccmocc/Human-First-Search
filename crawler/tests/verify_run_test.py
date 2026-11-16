"""Corruption and portability checks for the independent saved-run verifier."""

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

TOOL = Path(__file__).resolve().parents[2] / "tools" / "verify_run.py"
sys.path.insert(0, str(TOOL.parent))
from verify_run import verify_run


class VerifyRun(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        root = Path(self.directory.name)
        self.corpus = root / "saved corpus.jsonl"
        self.log = root / "saved journal.jsonl"
        self.manifest = root / "run.json"
        self.corpus.write_bytes('{"schema_version":1,"text":"Caf\u00e9"}\n'.encode("utf-8"))
        self.log.write_bytes(b'{"schema_version":1,"event":"crawl_target","outcome":"page_delivered"}\n'
                             b'{"schema_version":1,"event":"crawl_target","outcome":"fetch_failed"}\n')
        self.record = {"schema_version": 1, "completed": True,
            "summary": {"stop": "queue_empty", "pages": 1, "targets": 2, "fetch_failures": 1,
                        "skipped_responses": 0, "empty_texts": 0, "duplicate_pages": 0, "duplicate_texts": 0,
                        "non_english_pages": 0, "unknown_language_pages": 0, "rejected_seeds": 0, "pending_urls": 0},
            "artifacts": {"corpus": self.descriptor(self.corpus), "journal": self.descriptor(self.log)}}
        self.save_manifest()

    def descriptor(self, path):
        raw = path.read_bytes()
        return {"path": path.as_posix(), "bytes": len(raw), "sha256": "sha256:" + hashlib.sha256(raw).hexdigest()}

    def save_manifest(self):
        self.manifest.write_text(json.dumps(self.record), encoding="utf-8")

    def run_tool(self, *arguments):
        return subprocess.run([sys.executable, str(TOOL), *map(str, arguments)],
                              capture_output=True, text=True, encoding="utf-8", timeout=5)

    def audit_page(self, url="https://example.org/first", text=None):
        text = text if text is not None else "The library welcomes readers and helps them find useful books. Caf\u00e9.\n" * 4
        return {"schema_version": 1, "url": url, "text": text,
                "page_id": "sha256:" + hashlib.sha256(url.encode()).hexdigest(),
                "text_hash": "sha256:" + hashlib.sha256(text.encode()).hexdigest(),
                "extractor_version": "semantic-text-v3", "url_normalization_version": "lexbor-url-v1",
                "language_detection_version": "cld2-compact-v1", "text_source": "heading",
                "http_status": 200, "depth": 1, "requested_urls": [url], "language": "en", "language_percent": 99}

    def store_pages(self, pages):
        self.corpus.write_bytes(b"".join((json.dumps(page, ensure_ascii=True) + "\n").encode() for page in pages))
        self.record["summary"].update(pages=len(pages), targets=len(pages), fetch_failures=0)
        self.record["artifacts"] = {"corpus": self.descriptor(self.corpus), "journal": None}
        self.save_manifest()

    def enable_page_audit(self):
        self.record["configuration"] = {"allowed_hosts": ["example.org", "other.example", "[::1]"],
                                        "language": "en", "max_depth": 3}
        self.record["processing"] = {"extractor": "semantic-text-v3", "url_normalization": "lexbor-url-v1",
                                     "language_detection": "cld2-compact-v1"}
        self.store_pages([self.audit_page()])

    def test_valid_files_are_verified_without_modification(self):
        before = [path.read_bytes() for path in (self.manifest, self.corpus, self.log)]
        report = verify_run(self.manifest)
        self.assertTrue(report["verified"])
        self.assertEqual(report["artifacts"]["corpus"]["records"], 1)
        self.assertEqual(report["artifacts"]["journal"]["records"], 2)
        result = self.run_tool(self.manifest)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout), report)
        self.assertEqual(result.stderr, "")
        self.assertEqual([path.read_bytes() for path in (self.manifest, self.corpus, self.log)], before)

    def test_detects_same_size_changes_truncation_and_missing_files(self):
        original = self.corpus.read_bytes()
        for raw, diagnostic in ((original.replace(b"Caf", b"Bar"), "SHA-256"),
                                (original + original, "byte count"), (original[:-1], "LF-terminated")):
            with self.subTest(diagnostic=diagnostic):
                self.corpus.write_bytes(raw)
                with self.assertRaisesRegex(ValueError, diagnostic):
                    verify_run(self.manifest)
        self.corpus.unlink()
        with self.assertRaises(OSError):
            verify_run(self.manifest)

    def test_rejects_malformed_jsonl_even_when_its_hash_is_recorded(self):
        for raw in (b'{}\n', b'[]\n', b'{"schema_version":true}\n', b'{"schema_version":2}\n',
                    b'{"schema_version":1,"schema_version":1}\n', b'{"schema_version":1,"n":NaN}\n',
                    b'\xef\xbb\xbf{"schema_version":1}\n', b'{"schema_version":1}\r\n', b'\n',
                    b'{"schema_version":1,"text":"\xff"}\n', b'{broken}\n'):
            with self.subTest(raw=raw):
                self.corpus.write_bytes(raw)
                self.record["artifacts"]["corpus"] = self.descriptor(self.corpus)
                self.save_manifest()
                with self.assertRaises(ValueError):
                    verify_run(self.manifest)

    def test_detects_record_and_outcome_count_mismatches(self):
        self.corpus.write_bytes(self.corpus.read_bytes() * 2)
        self.record["artifacts"]["corpus"] = self.descriptor(self.corpus)
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, "record count"):
            verify_run(self.manifest)
        self.record["artifacts"]["corpus"] = None
        self.log.write_bytes(self.log.read_bytes().replace(b"fetch_failed", b"empty_text"))
        self.record["artifacts"]["journal"] = self.descriptor(self.log)
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, "fetch_failed count"):
            verify_run(self.manifest)
        self.log.write_bytes(b'{"schema_version":1,"event":"other","outcome":"unknown"}\n')
        self.record["artifacts"]["journal"] = self.descriptor(self.log)
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, "crawl_target"):
            verify_run(self.manifest)

    def test_copied_artifacts_use_overrides_without_changing_recorded_hashes(self):
        copied = self.corpus.with_name("copied corpus.jsonl")
        self.corpus.rename(copied)
        result = self.run_tool(self.manifest, "--corpus", copied, "--log", self.log)
        self.assertEqual(result.returncode, 0, result.stderr)
        copied.write_bytes(copied.read_bytes().replace(b"Caf", b"Bar"))
        result = self.run_tool(self.manifest, "--corpus", copied)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertIn("SHA-256", result.stderr)

    def test_absent_and_empty_artifacts_are_distinguished(self):
        self.corpus.write_bytes(b"")
        self.record["summary"].update(pages=0, targets=1)
        self.record["artifacts"]["corpus"] = self.descriptor(self.corpus)
        self.record["artifacts"]["journal"] = None
        self.save_manifest()
        report = verify_run(self.manifest)
        self.assertEqual(report["artifacts"]["corpus"]["records"], 0)
        self.assertNotIn("journal", report["artifacts"])
        with self.assertRaisesRegex(ValueError, "absent"):
            verify_run(self.manifest, log_path=self.log)
        self.record["artifacts"]["corpus"] = None
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, "no saved corpus or journal"):
            verify_run(self.manifest)

    def test_rejects_invalid_manifests_and_reports_cli_failures_without_tracebacks(self):
        valid = json.dumps(self.record)
        for key, value in (("completed", False), ("schema_version", True), ("summary", []), ("artifacts", {})):
            with self.subTest(key=key):
                broken = json.loads(valid)
                broken[key] = value
                self.manifest.write_text(json.dumps(broken), encoding="utf-8")
                with self.assertRaises(ValueError):
                    verify_run(self.manifest)
        self.record["summary"]["pages"] = True
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, "nonnegative integer"):
            verify_run(self.manifest)
        self.record["summary"]["pages"] = 8
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, "add up"):
            verify_run(self.manifest)
        for raw in (b"", b"{broken", b'{"schema_version":1,"schema_version":1}'):
            self.manifest.write_bytes(raw)
            result = self.run_tool(self.manifest)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, "")
            self.assertTrue(result.stderr.startswith("error: "))
            self.assertNotIn("Traceback", result.stderr)
        self.assertEqual(self.run_tool().returncode, 2)

    def test_page_audit_checks_utf8_hashes_and_reports_host_and_source_counts(self):
        self.enable_page_audit()
        first = self.audit_page()
        second = self.audit_page("https://other.example/second", "Different text with \u00e9 and a control \x01.")
        second["text_source"] = "main"
        self.store_pages([first, second])
        before = self.corpus.read_bytes()
        result = self.run_tool(self.manifest, "--check-pages")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["page_audit"], {
            "checked_pages": 2, "pages_by_host": {"example.org": 1, "other.example": 1},
            "text_sources": {"heading": 1, "main": 1}, "text_characters": len(first["text"]) + len(second["text"])})
        self.assertEqual(self.corpus.read_bytes(), before)
        self.assertNotIn("page_audit", verify_run(self.manifest))

    def test_page_audit_rejects_wrong_ids_and_hashes_even_with_updated_file_fingerprint(self):
        self.enable_page_audit()
        for field, value in (("page_id", "sha256:" + "0" * 64), ("text_hash", "sha256:" + "1" * 64),
                             ("text", "Changed content")):
            with self.subTest(field=field):
                page = self.audit_page()
                page[field] = value
                self.store_pages([page])
                # Basic integrity passes: semantic auditing must detect the inconsistency.
                verify_run(self.manifest)
                with self.assertRaisesRegex(ValueError, "page_id|text_hash"):
                    verify_run(self.manifest, check_pages=True)

    def test_page_audit_rejects_duplicate_urls_and_texts_with_record_context(self):
        self.enable_page_audit()
        first = self.audit_page()
        for second, message in ((first, "Duplicate page_id"),
                                (self.audit_page("https://example.org/copy", first["text"]), "Duplicate text_hash")):
            self.store_pages([first, second])
            with self.assertRaisesRegex(ValueError, r":2: " + message):
                verify_run(self.manifest, check_pages=True)

    def test_page_audit_checks_processing_versions_depth_status_chain_and_language(self):
        self.enable_page_audit()
        for field, value in (("extractor_version", "other"), ("url_normalization_version", "other"),
                             ("language_detection_version", None), ("text_source", "unknown"),
                             ("http_status", 404), ("http_status", True), ("depth", 4), ("depth", True),
                             ("requested_urls", []), ("requested_urls", ["https://example.org/other"]),
                             ("requested_urls", ["https://outside.invalid/", "https://example.org/first"]),
                             ("requested_urls", ["https://example.org/bad\ud800", "https://example.org/first"]),
                             ("language", "de"), ("language", None), ("language_percent", True),
                             ("language_percent", 101), ("text", "")):
            with self.subTest(field=field, value=value):
                page = self.audit_page()
                page[field] = value
                self.store_pages([page])
                with self.assertRaises(ValueError):
                    verify_run(self.manifest, check_pages=True)

    def test_page_audit_checks_scope_without_trusting_host_names_in_query_strings(self):
        self.enable_page_audit()
        for url in ("https://outside.invalid/?next=example.org", "https://example.org.evil.invalid/",
                    "https://user:pass@example.org/", "https://example.org/a#fragment", "file:///example.org",
                    "https://example.org:99999/", "https://example.org/a b", "https://example.org/\\path"):
            with self.subTest(url=url):
                self.store_pages([self.audit_page(url)])
                with self.assertRaises(ValueError):
                    verify_run(self.manifest, check_pages=True)
        self.store_pages([self.audit_page("http://[::1]:8080/article")])
        self.assertEqual(verify_run(self.manifest, check_pages=True)["page_audit"]["pages_by_host"], {"[::1]": 1})

    def test_page_audit_supports_unknown_language_only_in_diagnostic_mode(self):
        self.enable_page_audit()
        self.record["configuration"]["language"] = "any"
        for code, percent in ((None, None), ("de", 95)):
            page = self.audit_page()
            page.update(language=code, language_percent=percent)
            self.store_pages([page])
            self.assertEqual(verify_run(self.manifest, check_pages=True)["page_audit"]["checked_pages"], 1)
        for code, percent in ((None, 0), ("de", None), ("", 95)):
            page.update(language=code, language_percent=percent)
            self.store_pages([page])
            with self.assertRaises(ValueError):
                verify_run(self.manifest, check_pages=True)
        del page["language"]
        self.store_pages([page])
        with self.assertRaisesRegex(ValueError, "Missing language"):
            verify_run(self.manifest, check_pages=True)

    def test_page_audit_requires_corpus_and_processing_metadata_but_accepts_empty_corpus(self):
        self.enable_page_audit()
        self.store_pages([])
        report = verify_run(self.manifest, check_pages=True)["page_audit"]
        self.assertEqual(report, {"checked_pages": 0, "pages_by_host": {}, "text_sources": {}, "text_characters": 0})
        self.record["artifacts"]["corpus"] = None
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, "requires a saved corpus"):
            verify_run(self.manifest, check_pages=True)
        self.enable_page_audit()
        self.record.pop("processing")
        self.save_manifest()
        result = self.run_tool(self.manifest, "--check-pages")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertNotIn("Traceback", result.stderr)

    def test_page_audit_honors_copied_corpus_override_and_rejects_unpaired_surrogates(self):
        self.enable_page_audit()
        copied = self.corpus.with_name("copied.jsonl")
        self.corpus.rename(copied)
        result = self.run_tool(self.manifest, "--check-pages", "--corpus", copied)
        self.assertEqual(result.returncode, 0, result.stderr)
        page = self.audit_page()
        page["text"] = "Broken\ud800"
        self.store_pages([page])
        with self.assertRaisesRegex(ValueError, ":1:"):
            verify_run(self.manifest, check_pages=True)


if __name__ == "__main__":
    unittest.main()
