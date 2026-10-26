#!/usr/bin/env python3
"""Compile SPIR-V to cached Metal libraries; embed only successful, complete output."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import fcntl
from pathlib import Path
import subprocess
import zlib
from functools import lru_cache


@lru_cache(maxsize=1)
def converter_digest(converter: str) -> bytes:
    return hashlib.sha256(Path(converter).read_bytes()).digest()


def compile_library(source: Path, converter: str) -> bytes:
    # Host targets and a cache prewarm may request the same library together.
    # Check the fingerprint under the lock so only one compiler writes it.
    with source.with_suffix(".metal.lock").open("a+b") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        return compile_library_locked(source, converter)


def compile_library_locked(source: Path, converter: str) -> bytes:
    metal = source.with_suffix(".metal")
    air = source.with_suffix(".air")
    library = source.with_suffix(".metallib")
    stamp = source.with_suffix(".metal.sha256")
    digest = hashlib.sha256(source.read_bytes() + converter_digest(converter)).hexdigest()
    if library.exists() and stamp.exists() and stamp.read_text() == digest:
        return library.read_bytes()
    subprocess.run([converter, str(source), str(metal)], check=True)
    subprocess.run(["xcrun", "-sdk", "macosx", "metal", "-std=metal3.1", "-O2", "-c", str(metal), "-o", str(air)], check=True)
    subprocess.run(["xcrun", "-sdk", "macosx", "metallib", str(air), "-o", str(library)], check=True)
    stamp.write_text(digest)
    return library.read_bytes()


def array(symbol: str, data: bytes) -> str:
    rows = [",".join(str(b) for b in data[i:i+32]) for i in range(0, len(data), 32)]
    return f"static const unsigned char {symbol}[] = {{\n" + ",\n".join(rows) + "\n};\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("kind", choices=["host", "cache", "warm"])
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--converter", required=True)
    parser.add_argument("--symbol")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if args.kind == "host":
        result = array(args.symbol, compile_library(args.input, args.converter))
    else:
        sources = sorted(args.input.glob("*.spv"))
        if not sources:
            raise RuntimeError("No guest SPIR-V shaders were generated")
        with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
            libraries = list(pool.map(lambda p: compile_library(p, args.converter), sources))
        if args.kind == "warm":
            print(f"Compiled {len(sources)} Metal libraries into the build cache.")
            return
        pieces = ['#include "renut_engine/gpu/shaders/metal_cache.h"\nnamespace renut::gpu {\n']
        rows = []
        payload = bytearray()
        for i, (source, data) in enumerate(zip(sources, libraries)):
            shader_hash, variant = source.stem.split("_")
            rows.append(f"{{0x{shader_hash}ull, {variant}, {len(payload)}, {len(data)}}}")
            payload.extend(data)
        compressed = zlib.compress(payload, 9)
        pieces.append(array("metal_bytes", compressed))
        pieces.append("const unsigned char *g_metal_cache_bytes = metal_bytes;\n")
        pieces.append(f"const size_t g_metal_cache_compressed_size = {len(compressed)};\n")
        pieces.append(f"const size_t g_metal_cache_size = {len(payload)};\n")
        pieces.append("const MetalCacheEntry g_metal_cache[] = {\n" + ",\n".join(rows) + "\n};\n")
        pieces.append(f"const size_t g_metal_cache_count = {len(rows)};\n}}\n")
        result = "".join(pieces)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(args.output.suffix + ".tmp")
    temporary.write_text(result)
    temporary.replace(args.output)


if __name__ == "__main__":
    main()
