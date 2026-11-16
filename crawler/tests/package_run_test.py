"""Exercise a transferred handoff and reject damaged or incomplete source runs."""

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools/package_run.py"
COMMIT = "b2a706069df65a4d91d5de6219d59ffd3c716053"


class PackageRun(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name).resolve()
        self.assertEqual(self.root.parent, Path(tempfile.gettempdir()).resolve())
        self.addCleanup(self.directory.cleanup)
        self.corpus = self.root / "source corpus.jsonl"
        self.log = self.root / "source journal.jsonl"
        self.manifest = self.root / "source manifest.json"
        self.output = self.root / "handoff archive.zip"
        url, text = "https://example.org/page", "A reader's guide. Caf\u00e9.\n" * 20
        self.page = {"schema_version": 1, "url": url, "text": text, "title": "Guide",
            "page_id": "sha256:" + hashlib.sha256(url.encode()).hexdigest(),
            "text_hash": "sha256:" + hashlib.sha256(text.encode()).hexdigest(),
            "extractor_version": "semantic-text-v4", "url_normalization_version": "lexbor-url-v1",
            "language_detection_version": "cld2-compact-v1", "text_source": "heading",
            "http_status": 200, "content_type": "text/html", "depth": 0, "requested_urls": [url],
            "language": "en", "language_percent": 99, "fetched_at": "2026-11-16T12:00:00Z"}
        self.corpus.write_bytes((json.dumps(self.page, ensure_ascii=False) + "\n").encode("utf-8"))
        self.log.write_bytes(b'{"schema_version":1,"event":"crawl_target","outcome":"page_delivered"}\n'
                             b'{"schema_version":1,"event":"crawl_target","outcome":"fetch_failed"}\n')
        self.record = {"schema_version": 1, "completed": True,
            "configuration": {"schema_version": 1, "seeds": [url], "allowed_hosts": ["example.org"],
                              "language": "en", "max_depth": 2, "max_pages": 10},
            "processing": {"extractor": "semantic-text-v4", "url_normalization": "lexbor-url-v1",
                           "language_detection": "cld2-compact-v1"},
            "summary": {"stop": "queue_empty", "pages": 1, "targets": 2, "fetch_failures": 1,
                "skipped_responses": 0, "empty_texts": 0, "duplicate_pages": 0, "duplicate_texts": 0,
                "non_english_pages": 0, "unknown_language_pages": 0, "rejected_seeds": 0, "pending_urls": 0},
            "artifacts": {"corpus": self.descriptor(self.corpus), "journal": self.descriptor(self.log)}}
        self.save_manifest()

    def descriptor(self, path):
        raw = path.read_bytes()
        return {"path": str(path), "bytes": len(raw), "sha256": "sha256:" + hashlib.sha256(raw).hexdigest()}

    def save_manifest(self):
        self.manifest.write_text(json.dumps(self.record), encoding="utf-8")

    def run_tool(self, *extra):
        return subprocess.run([sys.executable, "-B", str(TOOL), str(self.manifest),
            "--output", str(self.output), "--source-commit", COMMIT, *map(str, extra)],
            capture_output=True, text=True, encoding="utf-8", timeout=8)

    def assert_rejected(self, *extra):
        result = self.run_tool(*extra)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertTrue(result.stderr.startswith("error: "), result.stderr)
        self.assertNotIn("Traceback", result.stderr)
        self.assertEqual(result.stdout, "")
        self.assertFalse(self.output.exists())

    def test_archive_survives_transfer_without_original_files(self):
        # A producer's paths can be unusable on the recipient's machine.
        for descriptor in self.record["artifacts"].values():
            descriptor["path"] = "X:/absent-producer/artifact.jsonl"
        self.save_manifest()
        originals = {path.name: path.read_bytes() for path in (self.manifest, self.corpus, self.log)}
        result = self.run_tool("--corpus", self.corpus, "--log", self.log)
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(report["pages"], 1)
        self.assertEqual(report["bytes"], self.output.stat().st_size)
        self.assertEqual(report["sha256"], "sha256:" + hashlib.sha256(self.output.read_bytes()).hexdigest())
        self.assertEqual(report["source_commit"], COMMIT)
        self.assertEqual(originals, {path.name: path.read_bytes() for path in (self.manifest, self.corpus, self.log)})
        copied = self.root / "recipient"
        copied.mkdir()
        with zipfile.ZipFile(self.output) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(len(archive.namelist()), len(set(archive.namelist())))
            self.assertNotIn("docs/liubomyr-plan.md", archive.namelist())
            self.assertFalse(any(name.startswith(("data/", "build/", ".git/")) for name in archive.namelist()))
            archive.extractall(copied)
        self.assertEqual((copied / "manifest.json").read_bytes(), originals[self.manifest.name])
        self.assertEqual((copied / "corpus.jsonl").read_bytes(), originals[self.corpus.name])
        self.assertEqual((copied / "crawl-log.jsonl").read_bytes(), originals[self.log.name])
        self.assertEqual((copied / "SOURCE_COMMIT.txt").read_text().strip(), COMMIT)
        self.assertEqual(json.loads((copied / "config.json").read_bytes()), self.record["configuration"])
        for path in (self.manifest, self.corpus, self.log):
            path.unlink()
        verify = subprocess.run([sys.executable, "-I", "-B", "tools/verify_run.py", "manifest.json",
            "--corpus", "corpus.jsonl", "--log", "crawl-log.jsonl", "--check-pages"],
            cwd=copied, capture_output=True, text=True, encoding="utf-8", timeout=5)
        self.assertEqual(verify.returncode, 0, verify.stderr)
        self.assertEqual(json.loads(verify.stdout), json.loads((copied / "audit-report.json").read_bytes()))
        guide = (copied / "docs/indexer-handoff.md").read_text(encoding="utf-8")
        example = guide.split("```python\n", 1)[1].split("```", 1)[0]
        reader = subprocess.run([sys.executable, "-I", "-B", "-c", example], cwd=copied,
                                capture_output=True, text=True, encoding="utf-8", timeout=5)
        self.assertEqual(reader.returncode, 0, reader.stderr)
        self.assertEqual(reader.stdout.strip(), self.page["page_id"] + " " + self.page["url"])

    def test_corruption_and_forged_page_hash_are_not_packaged(self):
        self.corpus.write_bytes(self.corpus.read_bytes().replace(b"reader", b"writer"))
        self.assert_rejected()
        # Updating only the file fingerprint must not bypass the per-page audit.
        self.record["artifacts"]["corpus"] = self.descriptor(self.corpus)
        self.save_manifest()
        self.assert_rejected()

    def test_incomplete_runs_and_missing_journals_are_not_packaged(self):
        self.record["completed"] = False
        self.save_manifest()
        self.assert_rejected()
        self.record["completed"] = True
        self.record["artifacts"]["journal"] = None
        self.save_manifest()
        self.assert_rejected()
        self.record["artifacts"]["journal"] = self.descriptor(self.log)
        self.save_manifest()
        self.log.unlink()
        self.assert_rejected()

    def test_existing_destination_is_never_overwritten(self):
        self.output.write_bytes(b"Keep this existing file")
        result = self.run_tool()
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("already exists", result.stderr)
        self.assertEqual(self.output.read_bytes(), b"Keep this existing file")

    def test_bad_revision_and_output_directory_fail_without_an_archive(self):
        self.assert_rejected("--source-commit", "HEAD")
        absent_parent = self.root / "not-created" / "archive.zip"
        result = self.run_tool("--output", absent_parent)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertFalse(absent_parent.exists())
        self.assertFalse(absent_parent.parent.exists())


if __name__ == "__main__":
    unittest.main()
