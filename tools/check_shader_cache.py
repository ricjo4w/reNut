#!/usr/bin/env python3
"""Exercise the Mac Xenos compiler's incremental cache with a locally supplied shader."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--shaders", type=Path, default=Path("shaders"))
    parser.add_argument("--common", type=Path,
        default=Path("thirdparty/reblue-XenosRecomp/XenosRecomp/shader_common.h"))
    args = parser.parse_args()
    compiler = args.compiler.resolve()
    sample = min(args.shaders.glob("vs_*.bin"), key=lambda p: p.stat().st_size)
    with tempfile.TemporaryDirectory(prefix="renut-cache-check-") as temp:
        root = Path(temp)
        inputs, outputs = root / "inputs", root / "outputs"
        inputs.mkdir(); outputs.mkdir()
        (inputs / sample.name).write_bytes(sample.read_bytes())
        common = root / "shader_common.h"
        common.write_bytes(args.common.read_bytes())

        def compile_shader(name):
            path = outputs / name
            subprocess.run([str(compiler), str(inputs), str(path), str(common)],
                check=True, stdout=subprocess.DEVNULL)
            return path.read_bytes()

        first = compile_shader("first.cpp")
        cache = next((outputs / "xenos_cache").glob("*/*.cache"))
        timestamp = cache.stat().st_mtime_ns
        if compile_shader("cached.cpp") != first or cache.stat().st_mtime_ns != timestamp:
            raise AssertionError("An unchanged shader was not reused identically")
        cache.write_bytes(b"corrupted test cache")
        if compile_shader("recovered.cpp") != first or cache.stat().st_size <= 32:
            raise AssertionError("Corrupted cache was not rebuilt identically")
        common.write_bytes(common.read_bytes() + b"\n// Cache invalidation check\n")
        compile_shader("changed-header.cpp")
        if len(list((outputs / "xenos_cache").iterdir())) != 2:
            raise AssertionError("Shader header changes did not invalidate the cache")
    print("Shader cache reuse, corruption recovery and header invalidation passed.")


if __name__ == "__main__":
    main()
