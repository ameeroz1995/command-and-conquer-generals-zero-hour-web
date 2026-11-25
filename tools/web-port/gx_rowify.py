#!/usr/bin/env python3
"""Round 5 helper: move a module class's per-unit data members into a GxComponentTable row.

Usage: gx_rowify.py <Header.h> <Source.cpp> <ClassName> [--check]

What it does (one module class, no subclasses outside its own .cpp):
- collects the class's non-static data members (m_*), removes them from the class and adds
  `Int m_gxRow;` instead;
- writes `struct Gx<ClassName>Row { ... }` (same types and names without the m_ prefix; value-
  initialised, i.e. zeroed like the memory-pool allocation the module had) and
  `extern GxComponentTable<...> TheGx<ClassName>s;` in front of the class;
- in the .cpp: defines the table and `#define m_x (TheGx...s.at(m_gxRow).x)` for every member right
  after the includes, turns the constructor's member-initialiser list into assignments, allocates the
  row first thing in the constructor and releases it at the end of the destructor.
It refuses (exit 2) and says why when a member name is also used outside this class's own code in a
way the macros would break (another type's field of the same name, use in the header after removal,
use in other .cpp files).
"""
import re, sys, os, subprocess

def die(msg):
    print("REFUSE:", msg); sys.exit(2)

hdr, src, cls = sys.argv[1], sys.argv[2], sys.argv[3]
check_only = '--check' in sys.argv
owner = next((a.split('=',1)[1] for a in sys.argv if a.startswith('--owner=')), 'getObject()')
add_dtor = '--add-dtor' in sys.argv
H = open(hdr).read(); S = open(src).read()

m = re.search(r'\nclass\s+' + cls + r'\b[^;{]*\{', H)
if not m: die("class not found in header")
start = m.end()
depth, i = 1, start
while depth:
    c = H[i]
    if c == '{': depth += 1
    elif c == '}': depth -= 1
    i += 1
body = H[start:i - 1]

members = []   # (type, name, arraysuffix, line)
nested = set()   # nested type / constant names declared at the class's top level
_depth = 0
for line in body.split('\n'):
    t = line.split('//')[0].strip()
    d0 = _depth
    _depth += t.count('{') - t.count('}')
    if d0 != 0:
        # enumerators of a top-level enum are class-scoped constants
        if d0 == 1 and re.match(r'^[A-Z_][A-Z0-9_]*\s*(=.*)?,?$', t):
            nested.add(re.match(r'^([A-Z_][A-Z0-9_]*)', t).group(1))
        continue
    nm = re.match(r'^(?:struct|class|enum|union)\s+(\w+)', t)
    if nm: nested.add(nm.group(1))
    tm = re.match(r'^typedef\s+.*\b(\w+)\s*;$', t)
    if tm: nested.add(tm.group(1))
    em = re.match(r'^enum\s*\{(.*)\}', t)
    if em:
        for e in em.group(1).split(','):
            e = e.strip().split('=')[0].strip()
            if e: nested.add(e)
    sc = re.match(r'^static\s+const\s+\w+\s+(\w+)\s*=', t)
    if sc: nested.add(sc.group(1))
    if not t or '(' in t or t.startswith(('static', 'typedef', 'friend', 'enum', 'using', 'public', 'protected', 'private', '#', 'MEMORY_POOL', 'MAKE_STANDARD')):
        continue
    mm = re.match(r'^(?P<type>(?:const\s+)?[\w:]+(?:\s*<[^;]*>)?(?:\s*[\*&]+)?)\s+(?P<rest>[\*&]*\s*m_\w+.*?)\s*(?P<init>=\s*[^;]+)?;$', t)
    if not mm:
        continue
    typ = mm.group('type').strip()
    init = (' ' + mm.group('init').strip()) if mm.group('init') else ''
    for part in mm.group('rest').split(','):
        part = part.strip()
        pm = re.match(r'^([\*&]*)\s*(m_\w+)\s*((?:\[[^\]]*\]\s*)*)\s*(?::\s*\d+)?$', part)
        if not pm: die("cannot parse member declaration: " + t)
        if pm.group(1).count('&'): die("reference member: " + t)
        members.append((typ + pm.group(1), pm.group(2), (pm.group(3) or '') + init, line))
if not members: die("no data members")

names = [n for _, n, _, _ in members]
# the header must not use the members outside their declarations once they are gone
rest_h = H[:start] + '\n'.join(l for l in body.split('\n') if all(l is not mm[3] for mm in members)) + H[i - 1:]
for n in names:
    decl_lines = [mm[3] for mm in members if mm[1] == n]
    hb = body
    for dl in decl_lines: hb = hb.replace(dl, '')
    pass  # inline uses in the header are rewritten to the row below
# other source files using these members (subclasses)
root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(src))))
if check_only:
    print("members:", ', '.join(names)); sys.exit(0)

row = 'Gx' + cls + 'Row'
table = 'TheGx' + cls + 's'
handle = 'm_gx' + cls + 'Row'
def qual(txt):
    for nn in sorted(nested, key=len, reverse=True):
        txt = re.sub(r'(?<![\w:])' + nn + r'\b', cls + '::' + nn, txt)
    return txt
def unconst(t):	# a const by-value member is set once by the constructor; the row field is plain
    return re.sub(r'^const\s+', '', t) if '*' not in t and '&' not in t else t
fields = '\n'.join(f'\t{qual(unconst(t))} {n[2:]}{qual(a)};' for t, n, a, _ in members)
struct = (f'// Web port (round 5, units are pure data): {cls}\'s per-unit state, one row per unit in {table};\n'
          f'// the module is a facade over its row (members read and write it through macros in its .cpp).\n'
          f'struct {row}\n{{\n{fields}\n}};\nextern GxComponentTable<{row}> {table};\n\n')
newbody = body
decls = set(mm[3] for mm in members)
newbody = '\n'.join(l if l in decls else re.sub(r'(?<!->)(?<![\w.])(' + '|'.join(names) + r')\b', lambda x: 'gxRow().' + x.group(1)[2:], l) for l in newbody.split('\n'))
first = True
for t, n, a, line in members:
    if line in newbody:
        newbody = newbody.replace(line + '\n', ('\tInt ' + handle + ';\t///< web port (round 5): this unit\'s row in ' + table + '\n'
            + '\tfriend struct ' + row + ';\n\tinline struct ' + row + ' &gxRow() const;\n') if first else '', 1)
        first = False
inc = ''
if '"GameLogic/GxComponents.h"' not in H:
    fi = re.search(r'^#include .*$', H, re.M)
    if fi:
        H = H[:fi.end()] + '\n#include "GameLogic/GxComponents.h"	// web port (round 5)' + H[fi.end():]
        m = re.search(r'\nclass\s+' + cls + r'\b[^;{]*\{', H); start = m.end()
        depth, i = 1, start
        while depth:
            c = H[i]
            if c == '{': depth += 1
            elif c == '}': depth -= 1
            i += 1
    else:
        inc = '#include "GameLogic/GxComponents.h"\n'
endcls = H.index(';', i - 1) + 1	# the class's closing '};'
after = f'\ninline {row} &{cls}::gxRow() const {{ return {table}.at({handle}); }}\n'
H2 = H[:m.start()] + '\n' + inc + f'struct {row};\n' + H[m.start() + 1:start] + newbody + H[i - 1:endcls] + '\n\n' + struct + after + H[endcls:]

# an inline constructor in the class body cannot allocate (the row type is incomplete there): move it to the .cpp
while True:
    cpos = re.search(r'\nclass\s+' + cls + r'\b[^;{]*\{', H2).start()
    cd, cend = 0, H2.index('{', cpos)
    cd, ce = 1, cend + 1
    while cd:
        if H2[ce] == '{': cd += 1
        elif H2[ce] == '}': cd -= 1
        ce += 1
    hb = re.search(r'\n(\s*)(explicit\s+)?' + cls + r'\s*\(([^)]*)\)\s*(:[^{]*)?\{', H2[cpos:ce])
    if hb:
        base = cpos
        a0 = base + hb.start() + 1
        d, e = 1, base + hb.end()
        while d:
            if H2[e] == '{': d += 1
            elif H2[e] == '}': d -= 1
            e += 1
        body_txt = H2[base + hb.end() - 1:e]
        args = hb.group(3)
        H2 = H2[:a0] + hb.group(1) + cls + '(' + args + ');\t///< web port (round 5): defined in the .cpp\n' + H2[e:].lstrip(';')
        args_def = re.sub(r'\s*=\s*[^,]+', '', args)
        inits = re.sub(r'gxRow\(\)\.(\w+)\s*\(', r'm_\1(', hb.group(4) or '')	# undo the header rewrite in the initialiser list
        body_txt = body_txt.replace('gxRow().', 'm_') if False else body_txt
        S = S.rstrip('\n') + '\n\n' + cls + '::' + cls + '(' + args_def + ')' + (' ' + inits.strip() if inits else '') + '\n' + body_txt + '\n'
    else:
        break
# .cpp: constructor initialiser list -> assignments, row allocation, release in destructor
def convert_ctor(S, ctor):
    inits = ctor.group(2) or ''
    parts, depth, cur = [], 0, ''
    for ch in inits:
        if ch == '(' : depth += 1
        if ch == ')' : depth -= 1
        if ch == ',' and depth == 0: parts.append(cur); cur = ''
        else: cur += ch
    parts.append(cur)
    keep, assigns = [], []
    for p in parts:
        p = p.strip()
        pm = re.match(r'^(m_\w+)\s*\((.*)\)$', p, re.S)
        if pm and pm.group(1) in names:
            assigns.append(f'\t{pm.group(1)} = {pm.group(2).strip()};' if pm.group(2).strip() else f'\t{pm.group(1)} = {{}};')
        elif p:
            keep.append(p)
    new = '\n' + cls + '::' + cls + '(' + ctor.group(1) + ')' + (' : ' + ', '.join(keep) if keep else '') + '\n{\n' + \
        f'\t{handle} = {table}.alloc( {owner} );\t// web port (round 5): this unit\'s row (zeroed, like the module was)\n' + \
        ('\n'.join(assigns) + '\n' if assigns else '')
    return S[:ctor.start()] + new + S[ctor.end():], new
S2, pos, ctor_new, nct = S, 0, None, 0
while True:
    ctor = re.compile(r'\n' + cls + r'::' + cls + r'\s*\(([^)]*)\)\s*(?::\s*(.*?))?\n?\{', re.S).search(S2, pos)
    if not ctor: break
    S2, nn = convert_ctor(S2, ctor)
    if ctor_new is None: ctor_new = nn
    pos = ctor.start() + len(nn); nct += 1
if not nct: die("constructor not found")
dtor = re.search(r'\n' + cls + r'::~' + cls + r'\s*\(\s*(void)?\s*\)\s*\{', S2)
if not dtor and add_dtor and re.search(r'EMPTY_DTOR\(\s*' + cls + r'\s*\)', H2):
    H2 = re.sub(r'EMPTY_DTOR\(\s*' + cls + r'\s*\)\s*\n', '', H2)	# declared by the memory-pool glue; now defined in the .cpp
    S2 = S2.rstrip('\n') + '\n\n' + cls + '::~' + cls + '()\n{\n}\n'
    dtor = re.search(r'\n' + cls + r'::~' + cls + r'\s*\(\s*(void)?\s*\)\s*\{', S2)
if not dtor and add_dtor:
    # declare one in the class (right after the row handle) and define it after the constructor
    H2 = H2.replace('\tInt ' + handle + ';', '\tInt ' + handle + ';\npublic:\n\tvirtual ~' + cls + '();\nprivate:', 1) if False else H2
    H2 = re.sub(r'(\nclass\s+' + cls + r'\b[^{]*\{)', r'\1\npublic:\n\tvirtual ~' + cls + '();\t///< web port (round 5): releases the row\n', H2, count=1)
    cpos = S2.find(ctor_new) + len(ctor_new)
    d, e = 1, cpos
    while d:
        if S2[e] == '{': d += 1
        elif S2[e] == '}': d -= 1
        e += 1
    S2 = S2[:e] + '\n\n' + cls + '::~' + cls + '()\n{\n}\n' + S2[e:]
    dtor = re.search(r'\n' + cls + r'::~' + cls + r'\s*\(\s*(void)?\s*\)\s*\{', S2)
if not dtor: die("destructor not found in the .cpp")
depth, j = 1, dtor.end()
while depth:
    if S2[j] == '{': depth += 1
    elif S2[j] == '}': depth -= 1
    j += 1
S2 = S2[:j - 1] + f'\t{table}.release( {handle} );\t// web port (round 5)\n' + S2[j - 1:]
incs = list(re.finditer(r'^#include .*$', S2, re.M))
pos = incs[-1].end()
head, tail = S2[:pos], S2[pos:]
# bare member uses (not x->m_name / x.m_name, which are other objects' or other types' fields) -> this unit's row
rowm = 'GXROW_' + cls.upper()
# rewrite bare member uses only inside this class's own member functions (Cls::...(...) { ... })
def rewrite_bodies(text):
    out, pos = [], 0
    for fm in re.finditer(r'\b' + cls + r'\s*::\s*~?\w+\s*\(', text):
        # find the body: the first '{' after the matching ')' (skipping initialiser lists), unless a ';' comes first
        k, depth = fm.end(), 1
        while depth and k < len(text):
            if text[k] == '(': depth += 1
            elif text[k] == ')': depth -= 1
            k += 1
        semi, brace = text.find(';', k), text.find('{', k)
        if brace < 0 or (0 <= semi < brace): continue
        if fm.start() < pos: continue
        d, e = 1, brace + 1
        while d and e < len(text):
            if text[e] == '{': d += 1
            elif text[e] == '}': d -= 1
            e += 1
        out.append(text[pos:fm.start()])
        seg = text[fm.start():e]
        seg = re.sub(r'(?<!->)(?<![\w.])(' + '|'.join(names) + r')\b', lambda x: rowm + '.' + x.group(1)[2:], seg)
        out.append(seg)
        pos = e
    out.append(text[pos:])
    return ''.join(out)
tail = rewrite_bodies(tail)
defs = (f'\n\n// Web port (round 5, units are pure data): {cls}\'s per-unit state is a row of {table};\n'
        f'// {rowm} is this unit\'s row (the old m_ members), so logic and xfer/CRC order are unchanged.\n'
        f'GxComponentTable<{row}> {table};\n#define {rowm} ({table}.at({handle}))\n')
S2 = head + defs + tail
open(hdr, 'w').write(H2); open(src, 'w').write(S2)
print("converted", cls, ":", ', '.join(names))
