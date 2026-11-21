#!/usr/bin/python3
"""Safely extract only the ONNX Runtime headers, dylib, and license notices."""
import pathlib
import stat
import sys
import tarfile

project = pathlib.Path(__file__).resolve().parent.parent
archive = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else project / "third_party/downloads/onnxruntime-osx-universal2-1.23.2.tgz"
destination = pathlib.Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else project / "third_party/onnxruntime"
if destination.exists() and any(destination.iterdir()):
    raise SystemExit("Destination must be empty")
destination.mkdir(parents=True, exist_ok=True)

with tarfile.open(archive, "r:gz") as package:
    members = package.getmembers()
    for member in members:
        path = pathlib.PurePosixPath(member.name)
        if path.is_absolute() or ".." in path.parts:
            raise SystemExit(f"Unsafe archive path: {member.name}")
    selected = []
    for member in members:
        path = pathlib.PurePosixPath(member.name)
        relative = pathlib.PurePosixPath(*path.parts[1:])
        wanted = (len(relative.parts) > 1 and relative.parts[0] == "include") or str(relative) in {
            "lib/libonnxruntime.1.23.2.dylib", "LICENSE", "ThirdPartyNotices.txt"
        }
        if wanted and member.isfile():
            selected.append((member, relative))
    for member, relative in selected:
        output = destination.joinpath(*relative.parts)
        if not output.resolve().is_relative_to(destination):
            raise SystemExit(f"Unsafe output path: {output}")
        output.parent.mkdir(parents=True, exist_ok=True)
        source = package.extractfile(member)
        if source is None:
            raise SystemExit(f"Cannot read archive member: {member.name}")
        with source, output.open("xb") as target:
            while chunk := source.read(1024 * 1024):
                target.write(chunk)
        output.chmod(0o755 if output.suffix == ".dylib" else stat.S_IMODE(member.mode))
print(f"Extracted {len(selected)} verified files to {destination}")
