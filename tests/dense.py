"""Check maximal valid line density and exact output at block boundaries."""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

line = b"2026-10-30T18:00:00.000000Z GET / 200 0\n"
assert len(line) == 40
for count in (1, 6553, 6554, 6569, 200000):
    with tempfile.TemporaryDirectory() as root:
        data, out = Path(root) / "data", Path(root) / "out"
        data.mkdir()
        out.mkdir()
        (data / "requests.log").write_bytes(line * count)
        env = dict(os.environ, SD_DATA_DIR=str(data), SD_OUT_DIR=str(out), SD_THREADS="6")
        subprocess.run([sys.argv[1]], env=env, check=True)
        expected = f"2026-10-30T18:00:00Z / {count} 0 0 0 0 0\n"
        assert (out / "summary.txt").read_text() == expected, count
    print("PASS dense", count, flush=True)
