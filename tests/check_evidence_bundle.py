"""Independently read exported archives with Python's standard TAR implementation."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile

with tempfile.TemporaryDirectory(prefix="sightline-evidence-") as directory:
    subprocess.run([sys.argv[1], directory], check=True)
    root = Path(directory)
    bundles = list((root / "exports").glob("*.tar"))
    assert len(bundles) == 3, "only successful exports should be published"
    assert not list((root / "exports").glob("*.tmp")), "partial archives should be removed"
    for bundle in bundles:
        with tarfile.open(bundle) as archive:
            manifest = json.load(archive.extractfile("bundle-manifest.json"))
            session_only = manifest["scope"] == "files attributed to the exact export context"
            assert len(manifest["files"]) == (1 if session_only else 3)
            assert len(archive.getmembers()) == (2 if session_only else 4)
            if session_only:
                assert manifest["files"][0]["path"] == "artifacts/scan.csv"
                assert manifest["files"][0]["context"]["project_id"] == "one"
            for entry in manifest["files"]:
                member = archive.getmember(entry["path"])
                assert member.isfile()
                content = archive.extractfile(member).read()
                assert content == (root / Path(member.name).name).read_bytes()
                assert len(content) == entry["size_bytes"]
                assert hashlib.sha256(content).hexdigest() == entry["sha256"]
print("Evidence inventory, preview, cancellation and archive interoperability checks passed")
