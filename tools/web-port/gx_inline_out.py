#!/usr/bin/env python3
"""Round 5 helper: after gx_rowify.py, inline methods in a class body that use gxRow() cannot compile
(the row struct is defined after the class). Move each such method (one line or several) out of the
class body into an `inline` definition placed after the class's `gxRow()` definition.
Only the named class's own body is touched.
Usage: gx_inline_out.py <Header.h> <ClassName>"""
import re, sys
hdr, cls = sys.argv[1], sys.argv[2]
H = open(hdr).read()
anchor = re.search(r'\ninline \w+ &' + cls + r'::gxRow\(\) const \{[^\n]*\}\n', H)
if not anchor:
    print("no gxRow definition"); sys.exit(0)
cm = re.search(r'\nclass\s+' + cls + r'\b[^;{]*\{', H)
if not cm:
    print("class not found"); sys.exit(0)
d, cend = 1, cm.end()
while d:
    if H[cend] == '{': d += 1
    elif H[cend] == '}': d -= 1
    cend += 1
body = H[cm.end():cend - 1]
head = re.compile(r'(?m)^(?P<ind>[ \t]*)(?P<virt>virtual\s+)?(?P<ret>(?:const\s+)?[\w:<>]+(?:\s*[\*&]+)?)\s*(?P<name>\w+)\s*\((?P<args>[^)]*)\)\s*(?P<cv>const)?\s*\{')
def pp_cond(text):
    # the preprocessor conditions in force at the end of `text` (within the class body)
    st = []
    for line in text.split('\n'):
        t = line.strip()
        if t.startswith('#ifdef'): st.append('defined(' + t.split()[1] + ')')
        elif t.startswith('#ifndef'): st.append('!defined(' + t.split()[1] + ')')
        elif t.startswith('#if'): st.append('(' + t[3:].strip() + ')')
        elif t.startswith('#elif'): st.append(None)
        elif t.startswith('#else') and st: st[-1] = None if st[-1] is None else '!' + st[-1]
        elif t.startswith('#endif') and st: st.pop()
    return st
out, moved, pos = [], [], 0
for m in head.finditer(body):
    if m.start() < pos or m.group('name') in ('gxRow', cls):
        continue
    conds = pp_cond(body[:m.start()])
    if None in conds:
        continue	# inside an #elif: leave it
    d, e = 1, m.end()
    while d:
        if body[e] == '{': d += 1
        elif body[e] == '}': d -= 1
        e += 1
    fbody = body[m.end() - 1:e]
    if 'gxRow()' not in fbody:
        continue
    # drop a trailing ';' after the body
    e2 = e
    while e2 < len(body) and body[e2] in ' \t': e2 += 1
    if e2 < len(body) and body[e2] == ';': e = e2 + 1
    args_def = re.sub(r'\s*=\s*[^,)]+', '', m.group('args'))
    cv = ' const' if m.group('cv') else ''
    out.append(body[pos:m.start()])
    out.append(f"{m.group('ind')}{m.group('virt') or ''}{m.group('ret')} {m.group('name')}({m.group('args')}){cv};")
    d = f"inline {m.group('ret')} {cls}::{m.group('name')}({args_def}){cv}\n{fbody}\n"
    if conds: d = '#if ' + ' && '.join(conds) + '\n' + d + '#endif\n'
    moved.append(d)
    pos = e
out.append(body[pos:])
H2 = H[:cm.end()] + ''.join(out) + H[cend - 1:anchor.end()] + ''.join(moved) + H[anchor.end():]
open(hdr, 'w').write(H2)
print(cls, "moved", len(moved))
