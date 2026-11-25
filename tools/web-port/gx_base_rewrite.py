#!/usr/bin/env python3
"""Round 5 helper: in a subclass's .cpp, rewrite bare uses of a converted base class's members
(m_x, now a field of the base's GxComponentTable row) to Base::gxRow().x, inside the subclass's own
member functions. Walks the inheritance chain through Include/GameLogic/Module/<Name>.h.
Usage: gx_base_rewrite.py <ModuleIncludeDir> <Source.cpp> <ClassName>"""
import re, sys, os
inc, src, cls = sys.argv[1], sys.argv[2], sys.argv[3]

ALL = None
if os.path.isfile(inc) or ',' in inc:	# a list of headers holding many classes
    ALL = ''.join(open(f).read() for f in inc.split(','))
def header_of(c):
    if ALL is not None:
        return ALL if re.search(r'\n[ \t]*class\s+' + c + r'\b', ALL) else None
    p = os.path.join(inc, c + '.h')
    return open(p).read() if os.path.exists(p) else None

def bases_of(c, text):
    m = re.search(r'\n[ \t]*class\s+' + c + r'\s*:\s*([^{]*)\{', text or '')
    if not m: return []
    return re.findall(r'(?:public|protected|private)\s+(\w+)', m.group(1))

def row_fields(c, text):
    m = re.search(r'\nstruct\s+Gx' + c + r'Row\s*\{(.*?)\n\};', text or '', re.S)
    if not m: return []
    return re.findall(r'\b(\w+)\s*(?:\[[^\]]*\]\s*)*;', m.group(1))

S = open(src).read()
chain, todo, seen = [], bases_of(cls, header_of(cls)), set()
while todo:
    b = todo.pop(0)
    if b in seen: continue
    seen.add(b)
    hb = header_of(b)
    f = row_fields(b, hb)
    if f: chain.append((b, f))
    todo += bases_of(b, hb)
if not chain:
    print(cls, ": no converted bases"); sys.exit(0)
total = 0
for b, fields in chain:
    names = ['m_' + f for f in fields]
    pat = re.compile(r'(?<!->)(?<![\w.])(' + '|'.join(names) + r')\b')
    out, pos = [], 0
    for fm in re.finditer(r'\b' + cls + r'\s*::\s*~?\w+\s*\(', S):
        if fm.start() < pos: continue
        k, depth = fm.end(), 1
        while depth and k < len(S):
            if S[k] == '(': depth += 1
            elif S[k] == ')': depth -= 1
            k += 1
        semi, brace = S.find(';', k), S.find('{', k)
        if brace < 0 or (0 <= semi < brace): continue
        d, e = 1, brace + 1
        while d and e < len(S):
            if S[e] == '{': d += 1
            elif S[e] == '}': d -= 1
            e += 1
        out.append(S[pos:fm.start()])
        seg, n = pat.subn(lambda x: f'{b}::gxRow().' + x.group(1)[2:], S[fm.start():e])
        total += n
        out.append(seg)
        pos = e
    out.append(S[pos:])
    S = ''.join(out)
open(src, 'w').write(S)
print(cls, ": rewrote", total, "base-member uses from", ', '.join(b for b, _ in chain))
