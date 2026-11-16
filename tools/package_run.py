"""Package a saved crawl for offline indexer ingestion, using only the standard library."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys
import tempfile
import zipfile

from verify_run import read_object, require, verify_run


ROOT = Path(__file__).resolve().parents[1]
# Deliberately enumerate public docs: never glob local notes or the personal plan.
DOCUMENTS = (
    "indexer-handoff.md", "corpus-format.md", "run-manifest.md", "crawl-config.md",
    "crawl-log-format.md", "html-parser.md", "heading-fallback.md", "h2-fallback.md",
    "frontier-scheduling.md", "frontier-admission.md", "pending-host-cap.md",
    "live-pilot-2026-11-13.md", "five-host-pilot-2026-11-13.md", "pilot-100-pages-2026-11-16.md",
)


def write_json(path, value):
    path.write_bytes((json.dumps(value, ensure_ascii=True, sort_keys=True, indent=2) + "\n").encode("utf-8"))


def prepare_payload(folder, manifest_path, corpus_path, log_path, source_commit):
    # Freeze the manifest first. Audit the copied bytes, not files that could change after verification.
    shutil.copyfile(manifest_path, folder / "manifest.json")
    manifest = read_object((folder / "manifest.json").read_bytes(), "manifest")
    artifacts = manifest.get("artifacts")
    require(isinstance(artifacts, dict), "Manifest has no artifacts object")
    for kind, override, name in (("corpus", corpus_path, "corpus.jsonl"),
                                 ("journal", log_path, "crawl-log.jsonl")):
        descriptor = artifacts.get(kind)
        require(isinstance(descriptor, dict), f"Packaging requires a saved {kind}")
        saved = descriptor.get("path")
        require(isinstance(saved, str) and saved, f"{kind}: missing artifact path")
        source = Path(override) if override is not None else Path(saved)
        require(override is not None or source.is_absolute(),
                f"{kind}: recorded path is not absolute on this OS; supply a path override")
        shutil.copyfile(source, folder / name)
    audit = verify_run(folder / "manifest.json", folder / "corpus.jsonl", folder / "crawl-log.jsonl",
                       check_pages=True)
    write_json(folder / "audit-report.json", audit)
    write_json(folder / "config.json", manifest["configuration"])
    (folder / "SOURCE_COMMIT.txt").write_bytes((source_commit + "\n").encode("ascii"))
    (folder / "tools").mkdir()
    shutil.copyfile(ROOT / "tools/verify_run.py", folder / "tools/verify_run.py")
    (folder / "docs").mkdir()
    for name in DOCUMENTS:
        shutil.copyfile(ROOT / "docs" / name, folder / "docs" / name)
    pages = audit["page_audit"]["checked_pages"]
    hosts = len(audit["page_audit"]["pages_by_host"])
    (folder / "README.md").write_bytes((
        "# Indexer handoff snapshot\n\n"
        f"This audited snapshot contains {pages} pages from {hosts} hosts.\n"
        "See docs/indexer-handoff.md for fields, a streaming reader and update rules.\n"
        "Historical pilot reports in docs/ describe their own samples, not this archive.\n\n"
        "From this unpacked directory, with Python 3:\n\n"
        "```sh\n"
        "python tools/verify_run.py manifest.json --corpus corpus.jsonl --log crawl-log.jsonl --check-pages\n"
        "```\n\n"
        "No crawler build or network access is needed for ingestion/auditing.\n"
        "Keep both overrides: manifest.json retains the original producer paths.\n"
        "config.json contains the manifest's effective crawl settings; other HTTP/robots\n"
        "options remain in manifest.json. The journal includes failed target attempts.\n\n"
        f"Declared crawler source commit: {source_commit}\n"
        "SOURCE_COMMIT.txt is supplied by the packager's caller. The manifest does not\n"
        "bind artifacts to a Git revision or prove which binary produced them.\n"
        "Docs and the verifier come from the packaging checkout. No native binary is included.\n"
        "A passing audit checks consistency, not extraction quality or indexer acceptance.\n"
    ).encode("utf-8"))
    return audit


def package_run(manifest_path, output_path, source_commit, corpus_path=None, log_path=None):
    require(re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", source_commit) is not None,
            "source-commit must be a full lowercase hexadecimal Git commit ID")
    output = Path(output_path).absolute()
    require(output.suffix.lower() == ".zip", "Output must have a .zip extension")
    require(not output.exists() and not output.is_symlink(), "Output already exists")
    parent = output.parent.resolve(strict=True)
    require(parent.is_dir(), "Output parent must be an existing directory")
    with tempfile.TemporaryDirectory(prefix=".hfs-package-", dir=parent) as temporary:
        workspace = Path(temporary).resolve()
        require(workspace.parent == parent, "Temporary workspace is outside the output directory")
        payload = workspace / "payload"
        payload.mkdir()
        audit = prepare_payload(payload, manifest_path, corpus_path, log_path, source_commit)
        draft = workspace / "handoff.zip"
        with zipfile.ZipFile(draft, "x", compression=zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(payload.rglob("*")):
                if path.is_file():
                    archive.write(path, path.relative_to(payload).as_posix())
        with zipfile.ZipFile(draft) as archive:
            require(archive.testzip() is None, "Archive CRC check failed")
        # Exclusive creation also handles another process creating the output during staging.
        destination = output.open("xb")
        try:
            with destination:
                with draft.open("rb") as source:
                    shutil.copyfileobj(source, destination)
        except BaseException:
            output.unlink()
            raise
    digest = hashlib.sha256()
    with output.open("rb") as stream:
        for chunk in iter(lambda: stream.read(65536), b""):
            digest.update(chunk)
    return {"archive": str(output), "bytes": output.stat().st_size,
            "sha256": "sha256:" + digest.hexdigest(), "source_commit": source_commit,
            "pages": audit["page_audit"]["checked_pages"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--output", type=Path, required=True, help="Fresh .zip path in an existing directory")
    parser.add_argument("--source-commit", required=True, help="Declared producing crawler revision; full Git hash")
    parser.add_argument("--corpus", type=Path, help="Location of a moved corpus; recorded hash still applies")
    parser.add_argument("--log", type=Path, help="Location of a moved journal; recorded hash still applies")
    arguments = parser.parse_args()
    try:
        report = package_run(arguments.manifest, arguments.output, arguments.source_commit,
                             arguments.corpus, arguments.log)
    except (OSError, ValueError, RecursionError, zipfile.BadZipFile) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
