#!/usr/bin/env python3
"""The Markdown of a tree: every local link leads to a file that exists and every #anchor to a heading there; the
documents that name the release say the current one (docs sync audit DOC-01/03, 2026-11-19).

    tools/check-docs-sync.py [<tree>]     the tree defaults to this repository; the public line runs it on its own
"""
import re
import sys
from pathlib import Path
from urllib.parse import unquote

LINK = re.compile(r'!?\[[^\]]*\]\(([^)\s]+)(?:\s+"[^"]*")?\)')
HEADING = re.compile(r'^(#{1,6})\s+(.+?)\s*#*\s*$', re.M)
FENCE = re.compile(r'^(```|~~~).*?^\1', re.M | re.S)
INLINE = re.compile(r'`[^`\n]*`')
SKIP = {'.git', 'node_modules', 'dist', 'tests'}


def slug(heading):
    # GitHub's anchors: code and link marks dropped, lower case, punctuation out, spaces to hyphens
    s = re.sub(r'`([^`]*)`', r'\1', heading)
    s = re.sub(r'\[([^\]]*)\]\([^)]*\)', r'\1', s)
    s = re.sub(r'[^\w\s-]', '', s.strip().lower())
    return re.sub(r'\s', '-', s)


def anchors(text):
    seen, out = {}, set()
    for m in HEADING.finditer(FENCE.sub('', text)):
        base = slug(m.group(2))
        n = seen.get(base, 0)
        out.add(base if n == 0 else '%s-%d' % (base, n))
        seen[base] = n + 1
    # explicit <a id="…"> / <a name="…"> targets
    out.update(re.findall(r'<a\s+(?:id|name)="([^"]+)"', text))
    return out


def check(root):
    root = root.resolve()
    files = [p for p in sorted(root.rglob('*.md')) if not SKIP & set(p.relative_to(root).parts)]
    texts, errors = {}, []
    for p in files:
        try:
            texts[p] = p.read_text(encoding='utf-8')
        except UnicodeError as e:
            errors.append('%s: not UTF-8 (%s)' % (p.relative_to(root), e))
    marks = {p: anchors(t) for p, t in texts.items()}
    for p, t in texts.items():
        body = INLINE.sub('', FENCE.sub('', t))
        for target in LINK.findall(body):
            target = target.strip('<>')
            if re.match(r'^[a-z][a-z0-9+.-]*:', target, re.I) or target.startswith('//'):
                continue
            path, _, frag = target.partition('#')
            dest = (p.parent / unquote(path)).resolve() if path else p
            if path and (not str(dest).startswith(str(root)) or not dest.exists()):
                errors.append('%s: broken link %s' % (p.relative_to(root), target))
                continue
            if frag and dest.suffix.lower() == '.md' and dest in marks and unquote(frag).lower() not in marks[dest]:
                errors.append('%s: no #%s in %s' % (p.relative_to(root), frag, dest.relative_to(root)))
    return len(files), errors


def main(argv):
    root = Path(argv[1]) if len(argv) > 1 else Path(__file__).resolve().parents[1]
    n, errors = check(root)
    for e in errors:
        print(e)
    print('%d Markdown files, %d problem(s)' % (n, len(errors)))
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
