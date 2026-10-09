"""Reader/writer for Source engine .vcs v6 shader containers.

Format cross-checked against:
  public/materialsystem/shader_vcs_version.h
  materialsystem/shaderapidx9/vertexshaderdx8.cpp (OpenFileAndLoadHeader,
      CreateDynamicCombos_Ver5)
  utils/shadercompile/shadercompile.cpp (format comment block ~line 640,
      FlushCombos/OutputDynamicCombo/WriteShaderFiles)
"""
import struct
import lzma

SENTINEL = 0xFFFFFFFF
MAX_UNPACKED_BLOCK = 1 << 17


def _lzma_decompress(payload, actual_size):
    """Valve lzma_header_t: id(4) actualSize(4) lzmaSize(4) properties(5)."""
    assert payload[:4] == b'LZMA', payload[:4]
    p = payload[12]
    lc = p % 9
    rest = p // 9
    lp = rest % 5
    pb = rest // 5
    dict_size = struct.unpack('<I', payload[13:17])[0]
    d = lzma.LZMADecompressor(
        format=lzma.FORMAT_RAW,
        filters=[{'id': lzma.FILTER_LZMA1, 'lc': lc, 'lp': lp, 'pb': pb,
                  'dict_size': dict_size}])
    # Valve's LzmaDecode runs with outProcessed = header actualSize, so the
    # header length is authoritative (the stream itself can decode one byte
    # further).
    out = d.decompress(payload[17:], max_length=actual_size)
    assert len(out) == actual_size, (len(out), actual_size)
    return out
FLAG_RAW = 0x80000000
FLAG_LZMA = 0x40000000
FLAG_MASK = 0xC0000000


class Header:
    def __init__(self, b):
        (self.version, self.total_combos, self.dynamic_combos, self.flags,
         self.centroid_mask, self.num_static, self.crc32) = struct.unpack('<7i', b[:28])
        assert self.version == 6, "unexpected vcs version %d" % self.version

    def pack(self):
        return struct.pack('<7i', self.version, self.total_combos,
                           self.dynamic_combos, self.flags, self.centroid_mask,
                           self.num_static, self.crc32)


class Vcs:
    """Parses a .vcs v6 file into:
         header
         static_records : list[(id, offset)] incl. sentinel
         dups           : list[(id, source_id)]
         sections       : {static_id: [ (dynamic_id, dxbc_bytes),
                                        ('dup', source_dynamic_id), ... ]}
       (a ('dup', src) entry is the 0x80000000-flagged dynamic duplicate
        record: second int is the source dynamic combo id)
    """

    def __init__(self, data):
        self.data = data
        self.header = Header(data[:28])
        p = 28
        n = self.header.num_static
        self.static_records = []
        for _ in range(n):
            sid, off = struct.unpack('<II', data[p:p + 8])
            self.static_records.append((sid, off))
            p += 8
        (self.nnum_dups,) = struct.unpack('<I', data[p:p + 4])
        p += 4
        self.dups = []
        for _ in range(self.nnum_dups):
            a, b = struct.unpack('<II', data[p:p + 8])
            self.dups.append((a, b))
            p += 8
        self.header_end = p
        # sentinel record offset = EOF
        sent_off = self.static_records[-1][1]
        assert self.static_records[-1][0] == SENTINEL, "no sentinel record"
        assert sent_off == len(data), "sentinel offset %d != filesize %d" % (sent_off, len(data))
        self.sections = {}
        for sid, off in self.static_records[:-1]:
            self.sections[sid] = self._read_section(off)

    def _read_section(self, off):
        data = self.data
        p = off
        out = []
        while True:
            (word,) = struct.unpack('<I', data[p:p + 4])
            p += 4
            if word == SENTINEL:
                break
            flag = word & FLAG_MASK
            size = word & 0x3FFFFFFF
            assert flag in (FLAG_RAW, FLAG_LZMA), "unsupported block flag 0x%08x" % word
            payload = data[p:p + size]
            p += size
            if flag == FLAG_RAW:
                out.extend(self._parse_block(payload))
            else:
                actual = struct.unpack('<I', payload[4:8])[0]
                out.extend(self._parse_block(_lzma_decompress(payload, actual)))
        assert p <= len(data)
        return out

    @staticmethod
    def _parse_block(buf):
        entries = []
        q = 0
        while q < len(buf):
            assert q + 8 <= len(buf), "truncated block header at %d/%d" % (q, len(buf))
            cid, sz = struct.unpack('<II', buf[q:q + 8])
            q += 8
            if cid & FLAG_RAW:           # dynamic duplicate record
                entries.append(('dup', cid & 0x7FFFFFFF, sz))
            else:
                assert q + sz <= len(buf), "entry %d overruns block" % cid
                entries.append(('combo', cid, buf[q:q + sz]))
                q += sz
        assert q == len(buf)
        return entries

    def combo_map(self):
        """{static_id: {dynamic_id: bytes}} (skips dup entries)"""
        m = {}
        for sid, entries in self.sections.items():
            d = {}
            for e in entries:
                if e[0] == 'combo':
                    assert e[1] not in d, "duplicate dynamic id"
                    d[e[1]] = e[2]
            m[sid] = d
        return m

    def dxbc(self, sid, did):
        for e in self.sections[sid]:
            if e[0] == 'combo' and e[1] == did:
                return e[2]
            if e[0] == 'dup' and e[1] == did:
                return self.dxbc(sid, e[2])
        raise KeyError((sid, did))


def _encode_entries(entries):
    """Pack entries into raw blocks honouring the writer's flush rule
    (shadercompile OutputDynamicCombo/FlushCombos): flush before an entry
    would push the buffer to >= MAX_UNPACKED_BLOCK."""
    blocks = []
    buf = bytearray()
    for e in entries:
        raw = (struct.pack('<II', 0x80000000 | e[1], e[2]) if e[0] == 'dup'
               else struct.pack('<II', e[1], len(e[2])) + e[2])
        if buf and len(buf) + len(raw) + 16 >= MAX_UNPACKED_BLOCK:
            blocks.append(bytes(buf))
            buf = bytearray()
        buf += raw
    if buf:
        blocks.append(bytes(buf))
    out = bytearray()
    for b in blocks:
        out += struct.pack('<I', FLAG_RAW | len(b))
        out += b
    out += struct.pack('<I', SENTINEL)
    return bytes(out)


def build(header, static_records, dups, sections):
    """Reassemble a .vcs from parsed parts. `sections` = [(sid, entries)] in
    the same entry form the parser produced."""
    body = bytearray()
    dict_size = 28 + 8 * len(static_records) + 4 + 8 * len(dups)
    rec_off = {}
    for sid, entries in sections:
        rec_off[sid] = dict_size + len(body)
        body += _encode_entries(entries)
    out = bytearray()
    out += header.pack()
    for sid, _old_off in static_records:
        if sid == SENTINEL:
            out += struct.pack('<II', sid, dict_size + len(body))
        else:
            out += struct.pack('<II', sid, rec_off[sid])
    out += struct.pack('<I', len(dups))
    for a, b in dups:
        out += struct.pack('<II', a, b)
    out += body
    return bytes(out)
