#!/usr/bin/env python3
"""List the framework (android.*) classes, methods and fields an APK references
that Android Translation Layer doesn't provide.

usage: atl-apicheck.py app.apk [atl-dex-dir] [core-dex-dir]
Resolution follows superclasses and interfaces through the app's own classes,
ATL's api-impl.jar and ART's core libraries, like the runtime does."""
import struct
import sys
import zipfile
from collections import defaultdict


def uleb(buf, off):
    result = shift = 0
    while True:
        b = buf[off]
        off += 1
        result |= (b & 0x7f) << shift
        if b < 0x80:
            return result, off
        shift += 7


class Dex:
    def __init__(self, data):
        self.d = data
        u = lambda off: struct.unpack_from('<I', data, off)[0]
        self.strings = self._strings(u(0x38), u(0x3c))
        n, off = u(0x40), u(0x44)
        self.types = [self.strings[struct.unpack_from('<I', data, off + 4 * i)[0]] for i in range(n)]
        n, off = u(0x48), u(0x4c)
        self.protos = []
        for i in range(n):
            _, ret, params = struct.unpack_from('<III', data, off + 12 * i)
            plist = ''
            if params:
                cnt = u(params)
                plist = ''.join(self.types[struct.unpack_from('<H', data, params + 4 + 2 * j)[0]] for j in range(cnt))
            self.protos.append('(%s)%s' % (plist, self.types[ret]))
        n, off = u(0x50), u(0x54)
        self.fields = []
        for i in range(n):
            c, t, name = struct.unpack_from('<HHI', data, off + 8 * i)
            self.fields.append((self.types[c], self.strings[name], self.types[t]))
        n, off = u(0x58), u(0x5c)
        self.methods = []
        for i in range(n):
            c, p, name = struct.unpack_from('<HHI', data, off + 8 * i)
            self.methods.append((self.types[c], self.strings[name], self.protos[p]))
        n, off = u(0x60), u(0x64)
        self.classes = {}
        self.code_offs = []
        for i in range(n):
            cls, _, sup, ifaces, _, _, cdata, _ = struct.unpack_from('<8I', data, off + 32 * i)
            name = self.types[cls]
            superclass = self.types[sup] if sup != 0xffffffff else None
            interfaces = []
            if ifaces:
                cnt = u(ifaces)
                interfaces = [self.types[struct.unpack_from('<H', data, ifaces + 4 + 2 * j)[0]] for j in range(cnt)]
            meths, flds = set(), set()
            if cdata:
                o = cdata
                sf, o = uleb(data, o)
                inf, o = uleb(data, o)
                dm, o = uleb(data, o)
                vm, o = uleb(data, o)
                for count, into in ((sf, flds), (inf, flds)):
                    idx = 0
                    for _ in range(count):
                        diff, o = uleb(data, o)
                        _, o = uleb(data, o)
                        idx += diff
                        f = self.fields[idx]
                        into.add((f[1], f[2]))
                for count in (dm, vm):
                    idx = 0
                    for _ in range(count):
                        diff, o = uleb(data, o)
                        _, o = uleb(data, o)
                        code, o = uleb(data, o)
                        if code:
                            self.code_offs.append(code)
                        idx += diff
                        m = self.methods[idx]
                        meths.add((m[1], m[2]))
            self.classes[name] = (superclass, interfaces, meths, flds)

    def _strings(self, n, off):
        out = []
        for i in range(n):
            soff = struct.unpack_from('<I', self.d, off + 4 * i)[0]
            _, p = uleb(self.d, soff)
            end = self.d.index(b'\0', p)
            out.append(self.d[p:end].decode('utf-8', 'replace'))
        return out


# Dalvik instruction widths in 16-bit code units, by opcode
_W = [1] * 256
for _op, _w in ((0x02, 2), (0x03, 3), (0x05, 2), (0x06, 3), (0x08, 2), (0x09, 3), (0x13, 2), (0x14, 3), (0x15, 2),
                (0x16, 2), (0x17, 3), (0x18, 5), (0x19, 2), (0x1a, 2), (0x1b, 3), (0x1c, 2), (0x1f, 2), (0x20, 2),
                (0x22, 2), (0x23, 2), (0x24, 3), (0x25, 3), (0x26, 3), (0x29, 2), (0x2a, 3), (0x2b, 3), (0x2c, 3),
                (0xfa, 4), (0xfb, 4), (0xfc, 3), (0xfd, 3), (0xfe, 2), (0xff, 2)):
    _W[_op] = _w
for _r, _w in (((0x2d, 0x38), 2), ((0x38, 0x3e), 2), ((0x44, 0x6e), 2), ((0x6e, 0x73), 3), ((0x74, 0x79), 3),
               ((0x90, 0xb0), 2), ((0xd0, 0xe3), 2)):
    for _op in range(*_r):
        _W[_op] = _w


def static_refs(dex):
    """(method idxs invoked with invoke-static, field idxs used with sget/sput)"""
    ms, fs = set(), set()
    d = dex.d
    for code in dex.code_offs:
        size = struct.unpack_from('<I', d, code + 12)[0]
        base = code + 16
        i = 0
        while i < size:
            unit = struct.unpack_from('<H', d, base + 2 * i)[0]
            op = unit & 0xff
            if op == 0 and unit:  # payload pseudo-instructions
                if unit == 0x100:
                    n = struct.unpack_from('<H', d, base + 2 * i + 2)[0]
                    i += n * 2 + 4
                elif unit == 0x200:
                    n = struct.unpack_from('<H', d, base + 2 * i + 2)[0]
                    i += n * 4 + 2
                elif unit == 0x300:
                    w = struct.unpack_from('<H', d, base + 2 * i + 2)[0]
                    n = struct.unpack_from('<I', d, base + 2 * i + 4)[0]
                    i += (n * w + 1) // 2 + 4
                else:
                    i += 1
                continue
            if op in (0x71, 0x77):
                ms.add(struct.unpack_from('<H', d, base + 2 * i + 2)[0])
            elif 0x60 <= op <= 0x6d:
                fs.add(struct.unpack_from('<H', d, base + 2 * i + 2)[0])
            i += _W[op]
    return ms, fs


def dexes_in(path):
    with zipfile.ZipFile(path) as z:
        for name in sorted(z.namelist()):
            if name.endswith('.dex'):
                yield Dex(z.read(name))


def main():
    apk = sys.argv[1]
    atl_dir = sys.argv[2] if len(sys.argv) > 2 else '/usr/local/lib/java/dex/android_translation_layer'
    core_dir = sys.argv[3] if len(sys.argv) > 3 else '/usr/lib/java/dex/art'
    import glob

    platform = {}
    for jar in sorted(glob.glob(core_dir + '/*.jar')) + [atl_dir + '/api-impl.jar']:
        for dex in dexes_in(jar):
            platform.update(dex.classes)

    app_classes = {}
    refs_m, refs_f = set(), set()
    for dex in dexes_in(apk):
        app_classes.update(dex.classes)
        refs_m.update(dex.methods)
        refs_f.update(dex.fields)

    def lookup(cls):
        # app classes shadow nothing in android.*; prefer the platform definition there
        if cls.startswith('Landroid/') and not cls.startswith(('Landroid/support/', 'Landroid/arch/')):
            return platform.get(cls)
        return app_classes.get(cls) or platform.get(cls)

    def resolves(cls, key, kind, seen=None):
        seen = seen if seen is not None else set()
        while cls and cls not in seen:
            seen.add(cls)
            c = lookup(cls)
            if c is None:
                return None  # class missing
            sup, ifaces, meths, flds = c
            if key in (meths if kind == 'm' else flds):
                return True
            for i in ifaces:
                if resolves(i, key, kind, seen):
                    return True
            cls = sup
        return False

    def touches_framework(cls):
        """does cls's superclass chain reach an android.* class?"""
        seen = set()
        while cls and cls not in seen:
            seen.add(cls)
            if cls.startswith('Landroid/') and not cls.startswith(('Landroid/support/', 'Landroid/arch/')):
                return True
            c = app_classes.get(cls)
            if not c:
                return False
            cls = c[0]
        return False

    missing_classes = set()
    missing = defaultdict(set)
    for kind, refs in (('m', refs_m), ('f', refs_f)):
        for cls, name, sig in refs:
            if not cls.startswith('L'):
                continue
            framework = cls.startswith('Landroid/') and not cls.startswith(('Landroid/support/', 'Landroid/arch/'))
            if not framework and not touches_framework(cls):
                continue
            r = resolves(cls, (name, sig), kind)
            if r is None:
                # find the first missing class in the chain
                c = cls
                while c and lookup(c):
                    c = lookup(c)[0]
                if c and c.startswith('Landroid/'):
                    missing_classes.add(c)
            elif r is False:
                # report against the first framework class in the chain
                c = cls
                while c and not (c.startswith('Landroid/') and not c.startswith(('Landroid/support/', 'Landroid/arch/'))):
                    c = (app_classes.get(c) or (None,))[0]
                missing[c or cls].add(('%s%s' if kind == 'm' else '%s:%s') % (name, sig))

    print('== missing classes (%d)' % len(missing_classes))
    for c in sorted(missing_classes):
        print(c[1:-1].replace('/', '.'))
    print('== missing members (%d in %d classes)' % (sum(len(v) for v in missing.values()), len(missing)))
    for c in sorted(missing):
        print(c[1:-1].replace('/', '.'))
        for m in sorted(missing[c]):
            print('    ' + m)


if __name__ == '__main__':
    main()
