#!/usr/bin/env python3
"""Require identical ordered bindings before replacing generated call unrolling."""
from pathlib import Path
import re
import sys

generated = Path(sys.argv[1])
registration = (generated / "renut_register.cpp").read_text()
initialization = (generated / "renut_init.cpp").read_text()
array = initialization.split("PPCFuncMapping PPCFuncMappings[] = {", 1)[1].split("};", 1)[0]
calls = re.findall(r"registrar->SetFunction\((0x[0-9A-Fa-f]+),\s*(\w+)\);", registration)
mappings = re.findall(r"\{\s*(0x[0-9A-Fa-f]+),\s*(\w+)\s*\}", array)
normalize = lambda rows: [(int(address, 16), function) for address, function in rows]
if (not calls or len(calls) != registration.count("registrar->SetFunction(")
    or array.count("{") != len(mappings) + 1 or normalize(calls) != normalize(mappings)
    or "{ 0, nullptr }" not in array):
    raise SystemExit("Generated function registration differs from PPCFuncMappings; refusing substitution")
print(f"Verified {len(calls)} identical ordered function bindings.")
