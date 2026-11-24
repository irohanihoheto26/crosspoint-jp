#!/usr/bin/env python3
"""Build a small Japanese Wikipedia StarDict dictionary (JaWiki).

Each article title becomes a headword; the definition is the first two
sentences of the lead paragraph. Source: the Hugging Face dataset
wikimedia/wikipedia (newest *.ja snapshot), CC BY-SA 4.0.

Usage: build_jawiki_dict.py <output folder>
Writes <out>/JaWiki.{ifo,idx,dict.dz} plus LICENSE.txt.
"""
import glob
import os
import re
import struct
import sys
import time
import zlib

import pyarrow.parquet as pq
from huggingface_hub import HfApi, snapshot_download

REPO = "wikimedia/wikipedia"
MAX_SENTENCES = 2
MAX_BYTES = 240
CHUNK = 58315  # dictzip's usual chunk length


def latest_config():
    api = HfApi()
    names = [e.path for e in api.list_repo_tree(REPO, repo_type="dataset") if e.path.endswith(".ja")]
    return max(names)


def lead_text(text):
    para = text.strip().split("\n", 1)[0].strip()
    sentences = re.findall(r"[^。]*。|[^。]+$", para)
    out = "".join(sentences[:MAX_SENTENCES]).strip()
    data = out.encode("utf-8")
    if len(data) > MAX_BYTES:
        out = data[:MAX_BYTES].decode("utf-8", "ignore") + "…"
    return out


def skip_title(title):
    return "(" in title or "（" in title or "一覧" in title or len(title.encode()) > 120


def write_dictzip(path, data):
    chunks = []
    comp = zlib.compressobj(9, zlib.DEFLATED, -15)
    body = bytearray()
    for start in range(0, len(data), CHUNK):
        part = data[start:start + CHUNK]
        last = start + CHUNK >= len(data)
        out = comp.compress(part) + comp.flush(zlib.Z_FINISH if last else zlib.Z_FULL_FLUSH)
        chunks.append(len(out))
        body += out
    if len(chunks) > 8192:
        sys.exit(f"too many dictzip chunks: {len(chunks)}")
    ra = struct.pack("<HHH", 1, CHUNK, len(chunks)) + b"".join(struct.pack("<H", c) for c in chunks)
    extra = b"RA" + struct.pack("<H", len(ra)) + ra
    header = b"\x1f\x8b\x08\x04" + struct.pack("<I", int(time.time())) + b"\x02\x03" + struct.pack("<H", len(extra))
    with open(path, "wb") as f:
        f.write(header + extra + body)
        f.write(struct.pack("<II", zlib.crc32(data) & 0xFFFFFFFF, len(data) & 0xFFFFFFFF))


def main():
    out_dir = sys.argv[1]
    os.makedirs(out_dir, exist_ok=True)
    config = latest_config()
    print("snapshot:", config)
    local = snapshot_download(REPO, repo_type="dataset", allow_patterns=[f"{config}/*.parquet"])

    entries = {}
    for file in sorted(glob.glob(os.path.join(local, config, "*.parquet"))):
        table = pq.read_table(file, columns=["title", "text"])
        for title, text in zip(table.column("title").to_pylist(), table.column("text").to_pylist()):
            if not title or not text or skip_title(title):
                continue
            definition = lead_text(text)
            if len(definition) >= 8:
                entries[title] = definition
        print(file, len(entries))

    words = sorted(entries, key=lambda w: (w.encode().lower(), w.encode()))
    data = bytearray()
    idx = bytearray()
    for w in words:
        d = entries[w].encode("utf-8")
        idx += w.encode() + b"\0" + struct.pack(">II", len(data), len(d))
        data += d

    base = os.path.join(out_dir, "JaWiki")
    with open(base + ".idx", "wb") as f:
        f.write(idx)
    write_dictzip(base + ".dict.dz", bytes(data))
    with open(base + ".ifo", "w", encoding="utf-8") as f:
        f.write("StarDict's dict ifo file\nversion=2.4.2\n")
        f.write(f"wordcount={len(words)}\nidxfilesize={len(idx)}\n")
        f.write("bookname=日本語Wikipedia\nsametypesequence=m\n")
        f.write(f"description=Lead sentences from Japanese Wikipedia ({config}), CC BY-SA 4.0\n")
    with open(os.path.join(out_dir, "LICENSE.txt"), "w", encoding="utf-8") as f:
        f.write("Text from Japanese Wikipedia, licensed CC BY-SA 4.0: https://creativecommons.org/licenses/by-sa/4.0/\n")
    print(f"{len(words)} entries, {len(data)} bytes of text")


if __name__ == "__main__":
    main()
