#!/usr/bin/env python3
# ===========================================================================
# tools/ungma.py - unpack a Garry's Mod addon archive.
#
# Handles both shapes that show up in the wild:
#
#   1. a plain .gma  ("GMAD" magic at offset 0)
#   2. what GMod's Workshop downloader writes today: the .gma is stored
#      LZMA-compressed as "<workshopid>_legacy.bin", which starts with a
#      classic LZMA-alone header (0x5D 0x00 0x00 0x00 <dictsize> <u64 size>)
#      - properties byte 0x5D, 32 MB dictionary, then the claimed size.
#      Decompressing it yields the "GMAD" archive.
#
# The header layout is NOT guessed.  Each candidate layout is parsed and then
# checked against the index it produces: the file numbers must be 1..N in
# order, every name must look like a path, the index must end with a zero file
# number, and the sum of the declared sizes must account for the rest of the
# file (a trailing file-number lookup table is allowed, so "not more than").
# Only a layout that passes is accepted.
#
#   python tools/ungma.py <archive> [<outdir>] [--list]
# ===========================================================================

import lzma
import os
import sys
import zlib

MAGIC = b"GMAD"
MAGIC_LZMA_PROPS = 0x5D


def read_cstring(buf, pos):
    end = buf.find(b"\x00", pos)
    if end < 0:
        raise ValueError("unterminated cstring at %d" % pos)
    return buf[pos:end].decode("utf-8", "replace"), end + 1


def looks_like_path(name):
    if not name or len(name) > 512:
        return False
    if "\x00" in name:
        return False
    if name.startswith(("/", "\\")) or ".." in name.split("/"):
        return False
    return any(c.isalnum() for c in name) and ("/" in name or "." in name)


def parse_index(buf, pos):
    """Read the file index; return (entries, data_start) or None if it does not
    hold together."""
    entries = []
    expected_number = 1
    while True:
        if pos + 4 > len(buf):
            return None
        number = int.from_bytes(buf[pos:pos + 4], "little")
        pos += 4
        if number == 0:
            break
        if number != expected_number:
            return None
        name, pos = read_cstring(buf, pos)
        if not looks_like_path(name):
            return None
        if pos + 12 > len(buf):
            return None
        size = int.from_bytes(buf[pos:pos + 8], "little", signed=True)
        crc = int.from_bytes(buf[pos + 8:pos + 12], "little")
        pos += 12
        if size < 0:
            return None
        entries.append((number, name, size, crc))
        expected_number += 1
    return entries, pos


def try_layout(buf, skip_after_version, cstring_count, has_addonversion):
    """Parse one candidate header layout; return a dict or None."""
    try:
        pos = 5  # magic + version
        pos += skip_after_version
        fields = []
        for _ in range(cstring_count):
            value, pos = read_cstring(buf, pos)
            fields.append(value)
        if has_addonversion:
            if pos + 4 > len(buf):
                return None
            pos += 4
        parsed = parse_index(buf, pos)
        if not parsed:
            return None
        entries, data_start = parsed
        total = sum(e[2] for e in entries)
        if data_start + total > len(buf):
            return None
        return {"fields": fields, "entries": entries, "data_start": data_start, "total": total}
    except ValueError:
        return None


def decompress_if_needed(data):
    """Return (gma_bytes, note). Handles a plain GMA and GMod's LZMA storage."""
    if data[:4] == MAGIC:
        return data, "plain .gma"
    if data[:1] == bytes([MAGIC_LZMA_PROPS]) and len(data) > 13:
        props = data[0]
        dict_size = int.from_bytes(data[1:5], "little")
        claimed = int.from_bytes(data[5:13], "little")
        out = lzma.LZMADecompressor(format=lzma.FORMAT_ALONE).decompress(data)
        note = ("LZMA-alone (props 0x%02X, dictionary %d MB, claimed %d bytes) -> %d bytes"
                % (props, dict_size // (1024 * 1024), claimed, len(out)))
        return out, note
    raise SystemExit("error: not a GMA and not LZMA-alone; first 16 bytes = %s"
                     % data[:16].hex(" "))


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    list_only = "--list" in argv
    if not args:
        print(__doc__)
        return 2

    src = args[0]
    outdir = args[1] if len(args) > 1 else os.path.splitext(os.path.basename(src))[0]

    with open(src, "rb") as fh:
        raw = fh.read()

    gma, note = decompress_if_needed(raw)
    print("source : %s (%d bytes)" % (src, len(raw)))
    print("decode : %s" % note)

    if gma[:4] != MAGIC:
        raise SystemExit("error: decompressed data is not a GMA (first 4 = %s)" % gma[:4].hex())

    version = gma[4]
    print("gma    : version %d" % version)

    # candidate layouts, tried until one validates
    candidates = []
    if version >= 3:
        candidates.append(("v3 (steamid+timestamp, 4 cstrings, addonversion)", 16, 4, True))
    candidates.append(("v1/v2 (no steamid/timestamp, 3 cstrings, addonversion)", 0, 3, True))
    candidates.append(("v1 (no steamid/timestamp, 3 cstrings, no addonversion)", 0, 3, False))
    candidates.append(("v3 without addonversion", 16, 4, False))

    parsed = None
    for label, skip, count, hasver in candidates:
        got = try_layout(gma, skip, count, hasver)
        if got:
            parsed = got
            print("header : %s" % label)
            break

    if not parsed:
        raise SystemExit("error: no candidate header layout produced a self-consistent index")

    fields = parsed["fields"]
    names = ("requiredcontent", "name", "description", "author") if len(fields) == 4 \
        else ("name", "description", "author")
    for key, value in zip(names, fields):
        if value:
            print("  %-15s %s" % (key, value))

    entries = parsed["entries"]
    print("files  : %d, %d bytes of data" % (len(entries), parsed["total"]))

    if list_only:
        for _num, name, size, _crc in entries:
            print("  %10d  %s" % (size, name))
        return 0

    os.makedirs(outdir, exist_ok=True)
    pos = parsed["data_start"]
    bad_crc = 0
    written = 0
    for _num, name, size, crc in entries:
        data = gma[pos:pos + size]
        pos += size
        if len(data) != size:
            raise SystemExit("error: truncated data for %s" % name)
        if zlib.crc32(data) & 0xFFFFFFFF != crc:
            bad_crc += 1
        safe = name.replace("\\", "/").lstrip("/")
        target = os.path.join(outdir, safe.replace("/", os.sep))
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, "wb") as fh:
            fh.write(data)
        written += 1

    print("wrote  : %d file(s) to %s" % (written, os.path.abspath(outdir)))
    if bad_crc:
        print("        %d file(s) had a mismatched CRC" % bad_crc)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
