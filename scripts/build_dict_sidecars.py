#!/usr/bin/env python3
"""Build the .qidx/.sidx lookup sidecars for a StarDict dictionary on a PC.

The reader builds these itself on first use (Dictionary::buildIndex), which is
slow for very large dictionaries. Output matches src/util/Dictionary.cpp
(SIDECAR_VERSION 2, SAMPLE_INTERVAL 256).

Usage: build_dict_sidecars.py <base path without extension>
"""
import os
import struct
import sys

SIDECAR_VERSION = 2
SAMPLE_INTERVAL = 256
QIDX_MAGIC = 0x58444951
SIDX_MAGIC = 0x58444953


def build(source, sidecar, magic, suffix_bytes):
    data = open(source, "rb").read()
    size = len(data)
    samples = [0]
    entries = 0
    pos = 0
    while pos < size:
        end = data.index(b"\0", pos)
        pos = end + 1 + suffix_bytes
        entries += 1
        if entries % SAMPLE_INTERVAL == 0 and pos < size:
            samples.append(pos)
    with open(sidecar, "wb") as f:
        f.write(struct.pack("<6I", magic, SIDECAR_VERSION, SAMPLE_INTERVAL, len(samples), size, entries))
        f.write(struct.pack("<%dI" % len(samples), *samples))
    print(f"{sidecar}: {entries} entries, {len(samples)} samples")


def main():
    base = sys.argv[1]
    build(base + ".idx", base + ".qidx", QIDX_MAGIC, 8)
    if os.path.exists(base + ".syn"):
        build(base + ".syn", base + ".sidx", SIDX_MAGIC, 4)


if __name__ == "__main__":
    main()
