#!/usr/bin/env python3
"""Round 5: move ai's component tables (GxComponentTable<Row> TheGx...s, GameLogic/GxComponents.h)
onto the shared pool (Common/GxComponentPool.h: GxComponentPool<Row>, GxRow<Row>).
- header: `struct GxFooRow *m_gx...Row;` -> `GxRow<struct GxFooRow> m_gx...Row;` (the GxRow member
  allocates the row when the module is built and frees it when it goes);
  `extern GxComponentTable<GxFooRow> TheGxFoos;` removed;
  `TheGxFoos.at(h)` -> `const_cast<GxFooRow &>(*h)`;
  include GameLogic/GxComponents.h -> Common/GxComponentPool.h.
- .cpp: the table definition, `h = TheGxFoos.alloc(...)`, `h( TheGxFoos.alloc(...) )` initialisers and
  `TheGxFoos.release( h );` removed; `TheGxFoos.at(h)` -> `const_cast<GxFooRow &>(*h)`.
Usage: gx_to_pool.py <files...>"""
import re, sys
tables = {}   # TheGxFoos -> GxFooRow
for f in sys.argv[1:]:
    for m in re.finditer(r'GxComponentTable<(\w+)>\s+(TheGx\w+)\s*;', open(f, errors='replace').read()):
        tables[m.group(2)] = m.group(1)
names = '|'.join(sorted(tables, key=len, reverse=True))
for f in sys.argv[1:]:
    s = open(f, errors='replace').read()
    o = s
    s = s.replace('#include "GameLogic/GxComponents.h"', '#include "Common/GxComponentPool.h"')
    s = re.sub(r'\n[^\n]*extern\s+GxComponentTable<\w+>\s+TheGx\w+\s*;[^\n]*', '', s)
    s = re.sub(r'\n[^\n]*\bGxComponentTable<\w+>\s+TheGx\w+\s*;[^\n]*', '', s)
    s = re.sub(r'struct (Gx\w+Row) \*(m_gx\w+Row)\s*;', r'GxRow<struct \1> \2;', s)
    s = re.sub(r'struct (GxMinefieldRow) \*(\s*)(m_gxRow)\s*;', r'GxRow<struct \1>\2\3;', s)
    if names:
        # remove allocation statements and initialisers, and releases
        s = re.sub(r'\n[ \t]*(m_gx\w*)\s*=\s*(?:' + names + r')\.alloc\([^;]*\);[^\n]*', '', s)
        s = re.sub(r',\s*\n?[ \t]*(m_gx\w*)\(\s*(?:' + names + r')\.alloc\(\s*(?:getObject\(\)|nullptr)\s*\)\s*\)', '', s)
        s = re.sub(r'\n[ \t]*(?:' + names + r')\.release\(\s*m_gx\w*\s*\);[^\n]*', '', s)
        s = re.sub(r'(' + names + r')\.at\(\s*(m_gx\w*)\s*\)', lambda m: f'const_cast<{tables[m.group(1)]} &>(*{m.group(2)})', s)
    if s != o:
        open(f, 'w').write(s)
print(len(tables), "tables")
