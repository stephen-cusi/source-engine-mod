#!/usr/bin/env python3
"""
to_utf8.py - convert legacy single-byte (latin-1) source files to UTF-8.

WHY THIS EXISTS
    Parts of this tree date from Team Sandbox / Valve sources that are stored as
    single-byte ANSI.  Their only non-ASCII characters are a handful of copyright
    signs (0xA9, latin-1 "(c)").  The file tools refuse to read such a file
    outright:

        Error: cannot read "...": invalid UTF-8 text

    The established remedy (AGENTS.md, the Lua SDK section) is to convert the
    file to UTF-8 rather than work around it with a codepage-specific reader.

SAFETY
    Only bytes in --allow (default: 0xA9) may appear outside ASCII.  If anything
    else does, the file is left untouched and reported, because a different byte
    may be a real character that needs a deliberate choice of source codepage --
    converting it blindly would corrupt text.

    A file that already decodes as UTF-8 (with or without BOM) is skipped.

USAGE
    python tools/to_utf8.py FILE [FILE ...]
    python tools/to_utf8.py --check FILE           # report only, change nothing
    python tools/to_utf8.py --allow a9,ae FILE     # widen the accepted byte set
    python tools/to_utf8.py --recursive DIR        # walk *.cpp/*.h/*.lua/...
"""

import argparse
import os
import sys

# Extensions worth walking in --recursive mode.
DEFAULT_GLOBS = (".cpp", ".h", ".c", ".lua", ".txt", ".vmt", ".vdf", ".res")


def parse_allow(text):
    allowed = set()
    for part in text.split(","):
        part = part.strip()
        if not part:
            continue
        allowed.add(int(part, 16))
    return allowed


def classify(data, allowed):
    """Return (verdict, detail).

    verdict is one of: 'ascii', 'utf8', 'convertible', 'foreign'.
    """
    if all(b < 0x80 for b in data):
        return "ascii", ""

    # Already valid UTF-8?  (BOM or not.)
    body = data[3:] if data[:3] == b"\xef\xbb\xbf" else data
    try:
        body.decode("utf-8")
        return "utf8", "already valid UTF-8"
    except UnicodeDecodeError:
        pass

    strangers = {}
    for b in data:
        if b > 0x7F and b not in allowed:
            strangers[b] = strangers.get(b, 0) + 1

    if strangers:
        shown = ", ".join("0x%02X x%d" % (b, n) for b, n in sorted(strangers.items()))
        return "foreign", "non-ASCII bytes outside --allow: " + shown

    return "convertible", ""


def convert(path, allowed, check_only):
    with open(path, "rb") as fh:
        data = fh.read()

    verdict, detail = classify(data, allowed)

    if verdict in ("ascii", "utf8"):
        return verdict, detail, False

    if verdict == "foreign":
        return verdict, detail, False

    if check_only:
        return verdict, "would convert", False

    # latin-1 maps every byte 0x00-0xFF to the same code point, so the decode
    # cannot fail and the only characters that change on the way out are the
    # allowed ones.
    text = data.decode("latin-1")
    with open(path, "wb") as fh:
        fh.write(text.encode("utf-8"))

    return verdict, detail, True


def iter_files(root):
    for dirpath, _dirnames, filenames in os.walk(root):
        for name in filenames:
            if name.lower().endswith(DEFAULT_GLOBS):
                yield os.path.join(dirpath, name)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Convert latin-1 sources to UTF-8.")
    ap.add_argument("paths", nargs="+", help="files, or directories with --recursive")
    ap.add_argument("--check", action="store_true", help="report only, do not write")
    ap.add_argument("--recursive", action="store_true", help="walk directories")
    ap.add_argument("--allow", default="a9",
                    help="comma separated hex bytes permitted outside ASCII (default a9)")
    args = ap.parse_args(argv)

    allowed = parse_allow(args.allow)

    targets = []
    for p in args.paths:
        if os.path.isdir(p):
            if not args.recursive:
                sys.stderr.write("skipping directory (use --recursive): %s\n" % p)
                continue
            targets.extend(iter_files(p))
        else:
            targets.append(p)

    counts = {"ascii": 0, "utf8": 0, "convertible": 0, "foreign": 0}
    converted = 0

    for path in targets:
        if not os.path.isfile(path):
            sys.stderr.write("no such file: %s\n" % path)
            counts["foreign"] += 1
            continue

        verdict, detail, wrote = convert(path, allowed, args.check)
        counts[verdict] = counts.get(verdict, 0) + 1

        if verdict == "foreign":
            print("FOREIGN   %s  (%s)" % (path, detail))
        elif verdict == "convertible":
            if wrote:
                print("CONVERTED %s" % path)
                converted += 1
            else:
                print("WOULD-CONVERT %s" % path)
        elif verdict == "utf8" and detail:
            print("ok        %s  (%s)" % (path, detail))

    print("")
    print("scanned=%d  converted=%d  already-utf8=%d  ascii=%d  foreign=%d"
          % (len(targets), converted, counts["utf8"], counts["ascii"], counts["foreign"]))

    return 1 if counts["foreign"] else 0


if __name__ == "__main__":
    sys.exit(main())
