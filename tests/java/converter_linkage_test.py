"""Which converter verification failures stop an app from starting: compiles the production rewriter and
runs tests/java/converter_linkage/LinkageCheck.java against its deferred-linkage rules."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/dex"))
from convert_apk import build_tools, java_path, java_tool

FIXTURE = ROOT / "tests/java/converter_linkage"

with tempfile.TemporaryDirectory(prefix="venture-linkage-") as directory:
    tools = java_path(build_tools())
    checks = Path(directory) / "classes"
    subprocess.run([java_tool("javac"), "-encoding", "UTF-8", "--release", "21", "-cp", tools, "-d", str(checks),
                    str(FIXTURE / "LinkageCheck.java")], check=True)
    subprocess.run([java_tool("java"), "-cp", os.pathsep.join([tools, str(checks)]), "com.venture.dex.LinkageCheck"], check=True)
