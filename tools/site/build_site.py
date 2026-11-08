#!/usr/bin/env python3
"""Build the project site: home page, downloads, rendered docs and the playable web build.

    python3 tools/site/build_site.py --output site-out [--web-build BUILD_DIR]

Needs the `markdown` package. The web build directory is the Emscripten build
(mksc.html, mksc.js, mksc.wasm, sw.js); without it the site has no /play/.
"""
import argparse
import html
import re
import shutil
from pathlib import Path

import markdown

ROOT = Path(__file__).resolve().parents[2]
SITE = Path(__file__).resolve().parent
REPO = 'arcanite24/mksc-neo'
REPO_URL = f'https://github.com/{REPO}'
DOCS = (  # source, output page, nav title
    ('docs/BUILDING.md', 'building.html', 'Building'),
    ('docs/MODDING.md', 'modding.html', 'Modding'),
    ('docs/ARCHITECTURE.md', 'architecture.html', 'Architecture'),
)
WEB_FILES = ('mksc.html', 'mksc.js', 'mksc.wasm', 'sw.js')


def page(title, body, current, prefix='', toc=''):
    nav = [('index.html', 'Home'), ('play/mksc.html', 'Play'), ('index.html#downloads', 'Downloads')]
    nav += [(f'docs/{out}', name) for _, out, name in DOCS]
    current_attr = ' aria-current="page"'
    links = ''.join(
        f'<a href="{prefix}{href}"{current_attr if href == current else ""}>{name}</a>'
        for href, name in nav)
    aside = f'<aside class="toc"><nav aria-label="On this page">{toc}</nav></aside>' if toc else ''
    template = (SITE / 'template.html').read_text(encoding='utf-8')
    return (template.replace('{{title}}', html.escape(title))
            .replace('{{prefix}}', prefix)
            .replace('{{nav}}', links)
            .replace('{{repo}}', REPO_URL)
            .replace('{{layout}}', 'doc' if toc else 'home')
            .replace('{{aside}}', aside)
            .replace('{{body}}', body))


def rewrite_links(text):
    """Point doc-to-doc links at the rendered pages and other repo paths at GitHub."""
    pages = {Path(src).name: out for src, out, _ in DOCS}

    def fix(match):
        target = match.group(1)
        if re.match(r'^[a-z]+:|^#|^/', target):
            return match.group(0)
        path, _, anchor = target.partition('#')
        name = Path(path).name
        if name in pages:
            return f'href="{pages[name]}{"#" + anchor if anchor else ""}"'
        resolved = (ROOT / 'docs' / path).resolve().relative_to(ROOT).as_posix()
        return f'href="{REPO_URL}/blob/main/{resolved}{"#" + anchor if anchor else ""}"'

    return re.sub(r'href="([^"]+)"', fix, text)


def render_doc(source):
    md = markdown.Markdown(extensions=['tables', 'fenced_code', 'toc', 'sane_lists'],
                           extension_configs={'toc': {'permalink': '#', 'toc_depth': '2-3'}})
    body = rewrite_links(md.convert((ROOT / source).read_text(encoding='utf-8')))
    edit = f'<p class="source">Source: <a href="{REPO_URL}/blob/main/{source}">{source}</a></p>'
    return body + edit, md.toc


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--web-build', type=Path)
    args = parser.parse_args()

    out = args.output
    if out.exists():
        shutil.rmtree(out)
    (out / 'docs').mkdir(parents=True)
    (out / 'assets').mkdir()
    shutil.copy2(SITE / 'site.css', out / 'assets/site.css')
    shutil.copy2(SITE / 'downloads.js', out / 'assets/downloads.js')
    shutil.copy2(ROOT / 'docs/branding/mksc-neo.png', out / 'assets/mksc-neo.png')
    (out / '.nojekyll').write_text('')

    home = (SITE / 'index.html').read_text(encoding='utf-8').replace('{{repo}}', REPO_URL)
    (out / 'index.html').write_text(page('MKSC Neo', home, 'index.html'), encoding='utf-8')

    for source, name, title in DOCS:
        body, toc = render_doc(source)
        (out / 'docs' / name).write_text(page(f'{title} · MKSC Neo', body, f'docs/{name}', '../', toc),
                                         encoding='utf-8')

    if args.web_build:
        play = out / 'play'
        play.mkdir()
        for name in WEB_FILES:
            path = args.web_build / name
            if not path.is_file():
                raise SystemExit(f'web build file missing: {path}')
            shutil.copy2(path, play / name)
    print(f'site written to {out}')


if __name__ == '__main__':
    main()
