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


if __name__ == "__main__":
    unittest.main()
