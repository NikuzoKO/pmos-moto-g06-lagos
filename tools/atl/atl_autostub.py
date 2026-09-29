#!/usr/bin/env python3
"""Insert no-op stubs into ATL's api-impl sources for every framework method/field
an APK references that ATL lacks. Each stub is one line tagged ATL-autostub, so
stubs that fail to compile can be removed by line (see --prune).

usage: atl_autostub.py app.apk            insert stubs
       atl_autostub.py --prune FILE:LINE...  remove the stubs at those lines"""
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atl_apicheck as ac

SRC = os.path.expanduser('~/src/atl/src/api-impl')
TAG = '// ATL-autostub'
PRIMS = {'V': 'void', 'Z': 'boolean', 'B': 'byte', 'S': 'short', 'C': 'char', 'I': 'int', 'J': 'long', 'F': 'float', 'D': 'double'}
DEFAULTS = {'boolean': 'false', 'char': "'\\0'", 'float': '0f', 'double': '0d', 'long': '0L'}


def parse_types(desc):
    out, i = [], 0
    while i < len(desc):
        j = i
        while desc[j] == '[':
            j += 1
        if desc[j] == 'L':
            end = desc.index(';', j)
            out.append(desc[i:end + 1])
            i = end + 1
        else:
            out.append(desc[i:j + 1])
            i = j + 1
    return out


def java_type(desc, known):
    dims = len(desc) - len(desc.lstrip('['))
    base = desc[dims:]
    if base in PRIMS:
        t = PRIMS[base]
    else:
        if base not in known:
            return None
        name = base[1:-1]
        if re.search(r'\$\d', name):
            return None
        t = name.replace('/', '.').replace('$', '.')
    return t + '[]' * dims


def main():
    apk = sys.argv[1]
    platform = {}
    for jar in sorted(glob.glob('/usr/lib/java/dex/art/*.jar')) + ['/usr/local/lib/java/dex/android_translation_layer/api-impl.jar']:
        for dex in ac.dexes_in(jar):
            platform.update(dex.classes)
    app_classes, refs_m, refs_f, static_m, static_f = {}, set(), set(), set(), set()
    for dex in ac.dexes_in(apk):
        app_classes.update(dex.classes)
        refs_m.update(dex.methods)
        refs_f.update(dex.fields)
        ms, fs = ac.static_refs(dex)
        static_m.update(dex.methods[i] for i in ms)
        static_f.update(dex.fields[i] for i in fs)

    def is_fw(c):
        return c.startswith('Landroid/') and not c.startswith(('Landroid/support/', 'Landroid/arch/'))

    def lookup(c):
        return platform.get(c) if is_fw(c) else (app_classes.get(c) or platform.get(c))

    def resolves(cls, key, kind, seen=None):
        seen = seen if seen is not None else set()
        while cls and cls not in seen:
            seen.add(cls)
            c = lookup(cls)
            if c is None:
                return None
            if key in (c[2] if kind == 'm' else c[3]):
                return True
            for i in c[1]:
                if resolves(i, key, kind, seen):
                    return True
            cls = c[0]
        return False

    def first_fw(cls):
        seen = set()
        while cls and cls not in seen:
            seen.add(cls)
            if is_fw(cls):
                return cls
            c = app_classes.get(cls)
            cls = c[0] if c else None
        return None

    known = set(platform)
    stubs = {}  # framework class -> set of java lines
    for kind, refs, statics in (('m', refs_m, static_m), ('f', refs_f, static_f)):
        for ref in refs:
            cls, name, sig = ref
            fw = first_fw(cls)
            if not fw or fw not in platform:
                continue
            if resolves(cls, (name, sig), kind) is not False:
                continue
            static = ref in statics
            if kind == 'm':
                if name == '<clinit>':
                    continue
                params_desc, ret_desc = sig[1:].split(')')
                params = [java_type(t, known) for t in parse_types(params_desc)]
                ret = java_type(ret_desc, known)
                if None in params or ret is None:
                    continue
                args = ', '.join('%s arg%d' % (t, n) for n, t in enumerate(params))
                if name == '<init>':
                    if fw != cls:
                        continue  # constructor refs name their exact class
                    simple = fw[1:-1].split('/')[-1].split('$')[-1]
                    line = 'public %s(%s) {} %s' % (simple, args, TAG)
                else:
                    if ret == 'void':
                        body = '{}'
                    elif ret in ('int', 'short', 'byte'):
                        body = '{ return 0; }'
                    else:
                        body = '{ return %s; }' % DEFAULTS.get(ret, 'null')
                    line = 'public %s%s %s(%s) %s %s' % ('static ' if static else '', ret, name, args, body, TAG)
            else:
                t = java_type(sig, known)
                if t is None:
                    continue
                line = 'public %s%s %s; %s' % ('static ' if static else '', t, name, TAG)
            stubs.setdefault(fw, set()).add(line)

    total = 0
    for fw, lines in sorted(stubs.items()):
        parts = fw[1:-1].split('$')
        path = os.path.join(SRC, parts[0] + '.java')
        if not os.path.exists(path):
            continue
        src = open(path).read()
        start = 0
        is_iface = False
        ok = True
        body_end = None
        # walk into nested classes
        m = re.search(r'\b(class|interface|enum)\s+%s\b[^{;]*\{' % re.escape(parts[0].split('/')[-1]), src)
        if not m:
            continue
        chain = [m]
        for inner in parts[1:]:
            m = re.compile(r'\b(class|interface|enum)\s+%s\b[^{;]*\{' % re.escape(inner)).search(src, chain[-1].end())
            if not m:
                ok = False
                break
            chain.append(m)
        if not ok:
            continue
        decl = chain[-1]
        is_iface = decl.group(1) == 'interface'
        if decl.group(1) == 'enum':
            continue
        depth, i = 1, decl.end()
        while depth and i < len(src):
            ch = src[i]
            if ch == '{':
                depth += 1
            elif ch == '}':
                depth -= 1
            elif ch == '"':
                i += 1
                while src[i] != '"':
                    i += 2 if src[i] == '\\' else 1
            elif ch == "'":
                i += 1
                while src[i] != "'":
                    i += 2 if src[i] == '\\' else 1
            elif src.startswith('//', i):
                i = src.index('\n', i)
            elif src.startswith('/*', i):
                i = src.index('*/', i) + 1
            i += 1
        body_end = i - 1
        out = []
        for line in sorted(lines):
            if is_iface:
                if '(' not in line or line.split('(')[0].split()[-1] == parts[-1]:
                    continue  # no fields or constructors in interfaces
                if not line.startswith('public static '):
                    line = line.replace('public ', 'public default ', 1)
            if line.split(TAG)[0].strip() in src:
                continue
            out.append('\t' + line + '\n')
        if not out:
            continue
        src = src[:body_end] + '\n' + ''.join(out) + src[body_end:]
        open(path, 'w').write(src)
        total += len(out)
    print('inserted %d stubs' % total)


def prune(locs):
    by_file = {}
    for loc in locs:
        path, line = loc.rsplit(':', 1)
        by_file.setdefault(path, set()).add(int(line))
    removed = 0
    for path, lines in by_file.items():
        src = open(path).read().split('\n')
        keep = []
        for n, l in enumerate(src, 1):
            if n in lines and TAG in l:
                removed += 1
                continue
            keep.append(l)
        open(path, 'w').write('\n'.join(keep))
    print('removed %d stubs' % removed)


if __name__ == '__main__':
    if sys.argv[1] == '--prune':
        prune(sys.argv[2:])
    else:
        main()
