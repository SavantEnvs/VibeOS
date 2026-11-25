#!/usr/bin/env python3
"""Generate the committed seed inputs under mayhem/<target>/testsuite/ (run by hand; the
build does not need it). Pure stdlib except the TTF seed, which needs fontTools and is
skipped if it is missing.  usage: gen_seeds.py <mayhem dir>"""
import os, struct, sys

M = sys.argv[1]

def put(target, name, data):
    d = os.path.join(M, target, "testsuite")
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, name), "wb") as f:
        f.write(data)

# ---------------- FAT32 ----------------
def short(name):  # "HELLO.TXT" -> 11 bytes
    if "." in name and name not in (".", ".."):
        b, e = name.split(".")
    else:
        b, e = name, ""
    return (b.ljust(8) + e.ljust(3)).encode()

def dirent(name11, attr, cluster, size):
    return struct.pack("<11sBBBHHHHHHHI", name11, attr, 0, 0, 0, 0, 0,
                       cluster >> 16, 0, 0, cluster & 0xFFFF, size)

def lfn_entries(longname, name11):
    csum = 0
    for c in name11:
        csum = (((csum & 1) << 7) + (csum >> 1) + c) & 0xFF
    chars = [ord(c) for c in longname] + [0]
    while len(chars) % 13:
        chars.append(0xFFFF)
    parts = [chars[i:i + 13] for i in range(0, len(chars), 13)]
    out = []
    for i, p in enumerate(parts):
        order = i + 1 + (0x40 if i == len(parts) - 1 else 0)
        out.append(struct.pack("<B10sBBB12sH4s", order, struct.pack("<5H", *p[0:5]), 0x0F, 0, csum,
                               struct.pack("<6H", *p[5:11]), 0, struct.pack("<2H", *p[11:13])))
    return b"".join(reversed(out))

def fat32_image(mbr=False):
    SPC, RES, NF, FATSZ = 1, 2, 1, 1
    clusters = {}
    fat = {0: 0x0FFFFFF8, 1: 0x0FFFFFFF}
    EOC = 0x0FFFFFFF
    hello = b"Hello from VibeOS FAT32!\n"
    big = bytes((i * 7) & 0xFF for i in range(1300))  # spans 3 clusters
    longtxt = b"long file name contents\n"
    notes = b"notes in /home/user\n"
    # cluster numbers
    ROOT, BIN, HOME, USER, HELLO, BIG0, LONG, NOTES = 2, 3, 4, 5, 6, 7, 10, 11
    root = (dirent(b"VIBEOS     ", 0x08, 0, 0) + dirent(short("BIN"), 0x10, BIN, 0) +
            dirent(short("HOME"), 0x10, HOME, 0) + dirent(short("HELLO.TXT"), 0x20, HELLO, len(hello)) +
            dirent(short("BIG.BIN"), 0x20, BIG0, len(big)) +
            lfn_entries("LongFileNameExample.txt", short("LONGFI~1.TXT")) +
            dirent(short("LONGFI~1.TXT"), 0x20, LONG, len(longtxt)) +
            b"\xe5" + dirent(short("DELETED.TXT"), 0x20, 0, 0)[1:])
    dot = lambda me, parent: dirent(b".          ", 0x10, me, 0) + dirent(b"..         ", 0x10, parent, 0)
    clusters[ROOT] = root
    clusters[BIN] = dot(BIN, 0) + dirent(short("A"), 0x20, 0, 0)
    clusters[HOME] = dot(HOME, 0) + dirent(short("USER"), 0x10, USER, 0)
    clusters[USER] = dot(USER, HOME) + dirent(short("NOTES.TXT"), 0x20, NOTES, len(notes))
    clusters[HELLO] = hello
    clusters[BIG0], clusters[8], clusters[9] = big[:512], big[512:1024], big[1024:]
    clusters[LONG] = longtxt
    clusters[NOTES] = notes
    for c in (ROOT, BIN, HOME, USER, HELLO, LONG, NOTES):
        fat[c] = EOC
    fat[BIG0], fat[8], fat[9] = 8, 9, EOC
    ncl = 16  # a few free clusters for the write paths
    total = RES + NF * FATSZ + ncl
    bpb = bytearray(512)
    bpb[0:3] = b"\xEB\x58\x90"
    bpb[3:11] = b"VIBEOS  "
    struct.pack_into("<HBHBHHBHHHII", bpb, 11, 512, SPC, RES, NF, 0, 0, 0xF8, 0, 0, 0, 0, total)
    struct.pack_into("<IHHIHH", bpb, 36, FATSZ, 0, 0, ROOT, 1, 0)
    bpb[64], bpb[66] = 0x80, 0x29
    bpb[71:82] = b"VIBEOS     "
    bpb[82:90] = b"FAT32   "
    if not mbr:  # raw volume: no 0x55AA, so the driver takes sector 0 as the BPB
        pass
    fs = bytearray(512)
    fs[0:4] = b"RRaA"
    fatsec = bytearray(512)
    for c, v in fat.items():
        struct.pack_into("<I", fatsec, c * 4, v)
    img = bytearray(bpb) + fs + fatsec
    for c in range(2, 2 + ncl):
        img += clusters.get(c, b"").ljust(512, b"\0")
    if mbr:
        mb = bytearray(512)
        struct.pack_into("<BBBBBBBBII", mb, 446, 0x80, 0, 0, 0, 0x0C, 0, 0, 0, 1, total)
        mb[510:512] = b"\x55\xAA"
        img = mb + img
    return bytes(img)

raw = fat32_image()
put("fuzz_fat32", "raw.img", raw)
put("fuzz_fat32", "mbr.img", fat32_image(mbr=True))
ops = bytes([0, 5, 0,  2, 0, 0,  3, 6, 0,  4, 6, 3,  0, 6, 0,  6, 10, 0,  1, 4, 0,  3, 11, 0,
             5, 11, 9,  10, 5, 1,  12, 7, 0,  7, 6, 0,  9, 2, 0,  11, 0, 20,  13, 3, 0,  8, 10, 0])
put("fuzz_vfs", "ops.bin", struct.pack("<H", len(raw)) + raw + ops)
put("fuzz_vfs", "mbr_ops.bin", struct.pack("<H", len(fat32_image(True))) + fat32_image(True) + ops[::-1])

# ---------------- DTB ----------------
def dtb():
    strings = b"#address-cells\0#size-cells\0reg\0device_type\0"
    so = {"#address-cells": 0, "#size-cells": 15, "reg": 27, "device_type": 31}
    st = b""
    be = lambda *v: struct.pack(">" + "I" * len(v), *v)
    def node(name): 
        n = name.encode() + b"\0"
        return be(1) + n.ljust((len(n) + 3) & ~3, b"\0")
    def prop(name, val):
        return be(3, len(val), so[name]) + val.ljust((len(val) + 3) & ~3, b"\0")
    st += node("") + prop("#address-cells", be(2)) + prop("#size-cells", be(2))
    st += node("memory@40000000") + prop("device_type", b"memory\0") + prop("reg", struct.pack(">QQ", 0x40000000, 0x20000000)) + be(2)
    st += be(2) + be(9)
    off_struct = 40 + 16
    off_str = off_struct + len(st)
    total = off_str + len(strings)
    hdr = be(0xd00dfeed, total, off_struct, off_str, 40, 17, 16, 0, len(strings), len(st))
    return hdr + b"\0" * 16 + st + strings
put("fuzz_dtb", "virt.dtb", dtb())

# ---------------- ELF (aarch64 PIE with one RELATIVE reloc) ----------------
def elf():
    ph_off, phnum = 64, 2
    code_off = 0x100
    code = b"\x1f\x20\x03\xd5" * 4 + b"\xc0\x03\x5f\xd6"   # nop x4; ret
    dyn_off = 0x140
    rela_off = 0x180
    data_off = 0x1a0
    dyn = struct.pack("<qQqQqQqQ", 7, rela_off, 8, 24, 9, 24, 0, 0)
    rela = struct.pack("<QQq", data_off, 0x403, code_off)
    filesz = data_off + 8
    eh = struct.pack("<16sHHIQQQIHHHHHH", b"\x7fELF\x02\x01\x01" + b"\0" * 9, 3, 183, 1, code_off,
                     ph_off, 0, 0, 64, 56, phnum, 64, 0, 0)
    load = struct.pack("<IIQQQQQQ", 1, 7, 0, 0, 0, filesz, filesz + 0x40, 0x1000)
    dynp = struct.pack("<IIQQQQQQ", 2, 6, dyn_off, dyn_off, dyn_off, len(dyn), len(dyn), 8)
    img = bytearray(filesz)
    img[0:64] = eh
    img[64:64 + 56] = load
    img[120:176] = dynp
    img[code_off:code_off + len(code)] = code
    img[dyn_off:dyn_off + len(dyn)] = dyn
    img[rela_off:rela_off + len(rela)] = rela
    return bytes(img)
put("fuzz_elf", "pie.elf", elf())

# ---------------- network frames ----------------
OUR_MAC = bytes.fromhex("525400123456"); GW_MAC = bytes.fromhex("525400000202")
OUR_IP = bytes([10, 0, 2, 15]); GW_IP = bytes([10, 0, 2, 2]); DNS_IP = bytes([10, 0, 2, 3])
def csum(b):
    if len(b) % 2: b += b"\0"
    s = sum(struct.unpack("!%dH" % (len(b) // 2), b))
    while s >> 16: s = (s & 0xFFFF) + (s >> 16)
    return (~s) & 0xFFFF
def eth(t, payload, src=GW_MAC): return OUR_MAC + src + struct.pack("!H", t) + payload
def ip(proto, payload, src=GW_IP):
    h = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + len(payload), 1, 0, 64, proto, 0, src, OUR_IP)
    h = h[:10] + struct.pack("!H", csum(h)) + h[12:]
    return h + payload
def frame(b): return struct.pack("<H", len(b)) + b
arp_reply = eth(0x0806, struct.pack("!HHBBH6s4s6s4s", 1, 0x0800, 6, 4, 2, GW_MAC, GW_IP, OUR_MAC, OUR_IP))
arp_req = eth(0x0806, struct.pack("!HHBBH6s4s6s4s", 1, 0x0800, 6, 4, 1, GW_MAC, GW_IP, b"\0" * 6, OUR_IP), )
icmp = struct.pack("!BBHHH", 8, 0, 0, 0x42, 1) + b"pingdata"
icmp = icmp[:2] + struct.pack("!H", csum(icmp)) + icmp[4:]
ping = eth(0x0800, ip(1, icmp))
dnsq = b"\x07example\x03com\x00\x00\x01\x00\x01"
dnsa = b"\xc0\x0c\x00\x01\x00\x01\x00\x00\x0e\x10\x00\x04\x5d\xb8\xd8\x22"
dnsmsg = struct.pack("!HHHHHH", 0x1234, 0x8180, 1, 1, 0, 0) + dnsq + dnsa
dns = eth(0x0800, ip(17, struct.pack("!HHHH", 53, 10053 + 0x1234 % 100, 8 + len(dnsmsg), 0) + dnsmsg, DNS_IP))
udp7 = eth(0x0800, ip(17, struct.pack("!HHHH", 1234, 7777, 8 + 5, 0) + b"hello"))
def tcp(flags, seq, ack, data=b""):
    h = struct.pack("!HHIIBBHHH", 80, 49152, seq, ack, 5 << 4, flags, 65535, 0, 0) + data
    ph = GW_IP + OUR_IP + struct.pack("!BBH", 0, 6, len(h))
    h = h[:16] + struct.pack("!H", csum(ph + h)) + h[18:]
    return eth(0x0800, ip(6, h))
synack = tcp(0x12, 9000, 5001)
data = tcp(0x18, 9001, 5001, b"HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\nok")
fin = tcp(0x11, 9001 + 40, 5001)
put("fuzz_net", "session.bin", b"".join(frame(f) for f in (arp_req, arp_reply, ping, dns, udp7, synack, data, fin)))
put("fuzz_net", "tcp.bin", b"".join(frame(f) for f in (synack, data)))

# ---------------- TTF ----------------
try:
    from fontTools.fontBuilder import FontBuilder
    from fontTools.pens.ttGlyphPen import TTGlyphPen
    def box(pen, x0, y0, x1, y1):
        pen.moveTo((x0, y0)); pen.lineTo((x0, y1)); pen.lineTo((x1, y1)); pen.lineTo((x1, y0)); pen.closePath()
    fb = FontBuilder(1000, isTTF=True)
    names = [".notdef", "space", "A", "B", "o"]
    fb.setupGlyphOrder(names)
    fb.setupCharacterMap({32: "space", 65: "A", 66: "B", 111: "o"})
    glyphs = {}
    for n in names:
        p = TTGlyphPen(None)
        if n == "A": p.moveTo((50, 0)); p.lineTo((300, 700)); p.lineTo((550, 0)); p.closePath()
        elif n == "B": box(p, 80, 0, 500, 700); box(p, 160, 380, 420, 620)
        elif n == "o": p.moveTo((100, 250)); p.qCurveTo((100, 500), (300, 500)); p.qCurveTo((500, 500), (500, 250)); p.qCurveTo((500, 0), (300, 0)); p.qCurveTo((100, 0), (100, 250)); p.closePath()
        elif n == ".notdef": box(p, 50, 0, 450, 700)
        glyphs[n] = p.glyph()
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics({n: (600, 50) for n in names})
    fb.setupHorizontalHeader(ascent=800, descent=-200)
    fb.setupNameTable({"familyName": "VibeSeed", "styleName": "Regular"})
    fb.setupOS2(); fb.setupPost()
    import io
    from fontTools.ttLib.tables._k_e_r_n import KernTable_format_0
    from fontTools.ttLib import newTable
    kern = newTable("kern"); kern.version = 0; st = KernTable_format_0(); st.coverage = 1; st.format = 0
    st.kernTable = {("A", "B"): -50, ("B", "o"): -20}; kern.kernTables = [st]; fb.font["kern"] = kern
    buf = io.BytesIO(); fb.font.save(buf); font = buf.getvalue()
    def reqs(lst): return bytes([len(lst)]) + b"".join(struct.pack("<HBB", cp, sz, st) for cp, sz, st in lst)
    put("fuzz_ttf", "plain.bin", reqs([(65, 8, 0), (66, 8, 0), (111, 16, 0), (32, 4, 0)]) + font)
    put("fuzz_ttf", "styled.bin", reqs([(65, 24, 2), (66, 24, 1), (111, 16, 3), (65, 8, 0x82)]) + font)
except ImportError:
    print("fontTools missing: TTF seeds not generated", file=sys.stderr)
