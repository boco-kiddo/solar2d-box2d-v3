# Generates an NDK-like stub shared library (C source + version script) from a bionic *.map.txt.
import re, sys
src, out_c, out_map = sys.argv[1:4]
nodes = []  # (name, parent, [(sym, isvar)])
cur = None
for raw in open(src):
    line = raw.strip()
    if not line:
        continue
    m = re.match(r'^([A-Z0-9_]+)\s*\{', line)
    if m:
        cur = [m.group(1), None, [], 'platform-only' in line or 'systemapi' in line]
        nodes.append(cur)
        continue
    m = re.match(r'^\}\s*([A-Z0-9_]*)\s*;', line)
    if m:
        if cur is not None and m.group(1):
            cur[1] = m.group(1)
        cur = None
        continue
    if cur is None or line.startswith('global:') or line.startswith('local:') or line.startswith('#') or line.startswith('*'):
        continue
    m = re.match(r'^([A-Za-z_][A-Za-z0-9_]*)\s*;(.*)$', line)
    if m and not re.search(r'platform-only|llndk|apex|systemapi', m.group(2)):
        cur[2].append((m.group(1), 'var' in m.group(2)))
seen = set()
with open(out_c, 'w') as c, open(out_map, 'w') as v:
    for name, parent, syms, private in nodes:
        if private or name.endswith('_PRIVATE') or name.endswith('_PLATFORM'):
            continue
        v.write(name + ' {\n  global:\n')
        for s, isvar in syms:
            v.write('    %s;\n' % s)
            if s in seen:
                continue
            seen.add(s)
            if isvar:
                c.write('char %s[1];\n' % s)
            else:
                c.write('void %s(void) {}\n' % s)
        v.write('}%s;\n' % ((' ' + parent) if parent and not parent.endswith('_PRIVATE') else ''))
    v.write('LIBSTUB_LOCAL { local: *; };\n')
