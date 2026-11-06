#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Builds the project's website (website/) and puts it on the branch
# gh-pages, which GitHub Pages serves at https://sorrelfox.github.io/reverblabr/.
#
# Usage:
#   tools/publish-website.sh
#       Builds the site from HEAD, from committed files only, and commits it
#       as one new commit on the local branch gh-pages (a branch with no
#       history the first time), signed off with this repository's identity.
#       The working tree, the index and the current branch are not touched.
#       The tag vX.Y.Z of the version has to exist, since the download links
#       go to its release. It never pushes: it prints the command that does.
#
#   tools/publish-website.sh --preview DIR
#       Builds the site from the working tree into DIR, to look at. DIR has to
#       be new, empty or an earlier preview. GitHub serves the site under
#       /reverblabr/, so serve it there too:
#           mkdir -p /tmp/pages && ln -sfn "$(realpath DIR)" /tmp/pages/reverblabr
#           python3 -m http.server -d /tmp/pages    # http://localhost:8000/reverblabr/
#
# A build copies website/, fills in @VERSION@ from VERSION, and @DATE@ and
# @DATE_TEXT@ from that release's date in the AppStream data. What the pages
# show from the rest of the tree is taken from there, so the repository holds
# no second copy of it:
#   images/NAME.webp      docs/images/NAME.png, as a lossless WebP
#   fonts/                the typefaces in app/fonts, cut down to the letters
#                         the site needs, as WOFF2, with their licence
#   icon.svg, favicon.*,  the application's icon (app/appicon)
#   apple-touch-icon.png
#   social.jpg            the picture a link to the site shows: the icon, the
#                         name and a screenshot
#   media/NAME-poster.webp  a frame of each clip in website/media, four
#                         seconds before its end
# Then it checks that every local link, image and anchor resolves, that every
# link into the repository at the version's tag names a file of that tag (of
# the working tree, for a preview), and that no page has an em-dash or a
# placeholder left. The same tree gives the same files, so a
# run with nothing new commits nothing.
#
# Needs python3 with fontTools and brotli, ImageMagick 7 (magick) with SVG and
# WebP, and ffmpeg.

set -euo pipefail

die() {
    printf 'publish-website: %s\n' "$*" >&2
    exit 1
}

usage() {
    sed -n '/^# Usage:/,/^# A build copies/p' "$0" | sed -e '$d' -e 's/^# \{0,1\}//'
}

preview=
case "${1:-}" in
"")
    [ $# -eq 0 ] || die "usage: tools/publish-website.sh [--preview DIR]"
    ;;
--preview)
    [ $# -eq 2 ] && [ -n "$2" ] || die "usage: tools/publish-website.sh --preview DIR"
    preview=$(realpath -m -- "$2")
    ;;
-h | --help)
    usage
    exit 0
    ;;
*)
    die "usage: tools/publish-website.sh [--preview DIR]"
    ;;
esac

root=$(cd "$(dirname "$0")" && git rev-parse --show-toplevel)
cd "$root"

for tool in python3 magick ffmpeg; do
    command -v "$tool" >/dev/null || die "needs $tool"
done

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# Python runs from the scratch folder, so nothing in the tree is imported.
py() {
    (cd "$work" && python3 "$@")
}
py -c 'import fontTools.subset, brotli' 2>/dev/null \
    || die "needs python3 with fontTools and brotli (python3-fonttools and python3-brotli)"

# ---- the tree to build from ----

if [ -n "$preview" ]; then
    src=$root
else
    # Every committed file of HEAD, through an index of its own: the real
    # index and the working tree stay as they are, and a path git archive
    # would leave out (export-ignore) is there too.
    src=$work/src
    mkdir -p "$src"
    GIT_INDEX_FILE=$work/head.index git read-tree HEAD
    GIT_INDEX_FILE=$work/head.index git checkout-index --all --prefix="$src/"
fi

[ -d "$src/website" ] || die "there is no website/ in the tree"

version=
read -r version <"$src/VERSION" || true
version=${version//[[:space:]]/}
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "VERSION says \"$version\", not MAJOR.MINOR.PATCH"

metainfo=$src/app/io.github.reverblabr.Reverblabr.metainfo.xml
date=$(sed -n "s/.*<release version=\"${version//./\\.}\" date=\"\([0-9-]*\)\".*/\1/p" "$metainfo")
date=${date%%$'\n'*}
if [ -z "$date" ]; then
    [ -n "$preview" ] || die "the AppStream data has no release $version (tools/release.sh prepare writes it)"
    date=$(date +%F)
    printf 'publish-website: the AppStream data has no release %s; the preview gives today as its date\n' "$version" >&2
fi
date_text=$(LC_ALL=C date -d "$date" '+%-d %B %Y')

# The pages link to the documentation as the version's tag has it, so that is
# the tree those links are checked against.
linked=$src
if [ -z "$preview" ]; then
    git rev-parse -q --verify "refs/tags/v$version" >/dev/null \
        || die "there is no tag v$version, so the download links would lead nowhere; tag the release first (tools/release.sh tag)"
    [ "$(git symbolic-ref -q HEAD || true)" != refs/heads/gh-pages ] \
        || die "HEAD is gh-pages; switch to the branch the site is built from"
    linked=$work/tag
    mkdir -p "$linked"
    GIT_INDEX_FILE=$work/tag.index git read-tree "refs/tags/v$version^{tree}"
    GIT_INDEX_FILE=$work/tag.index git checkout-index --all --prefix="$linked/"
fi

# ---- the build ----

# The letters the site is set in: Latin, its punctuation, and the few signs
# the pages use. A character outside them falls back to a system font.
unicodes='U+0020-007E,U+00A0-00FF,U+0131,U+0152-0153,U+02C6,U+02DA,U+02DC,U+2009-200B,U+2010-2011,U+2013,U+2018-201A,U+201C-201E,U+2022,U+2026,U+2032-2033,U+2039-203A,U+20AC,U+2122,U+2190-2193,U+2212,U+2248,U+2264-2265'

# subset FONT OUT [FEATURES]: the font, cut down and written as WOFF2. Every
# name record stays, so the copyright and the licence travel with it.
subset() {
    py -m fontTools.subset "$1" --output-file="$2" --flavor=woff2 --unicodes="$unicodes" \
        --name-IDs='*' --name-languages='*' --name-legacy --no-hinting \
        ${3:+--layout-features="$3"}
}

build() {
    local tree=$1 out=$2 name clip

    cp -R "$tree/website/." "$out/"
    find "$out" -name '*.license' -delete

    find "$out" -type f \( -name '*.html' -o -name '*.css' -o -name '*.js' \) -exec \
        sed -i -e "s/@VERSION@/$version/g" -e "s/@DATE@/$date/g" -e "s/@DATE_TEXT@/$date_text/g" {} +
    local left
    left=$(grep -rlE '@[A-Z_]+@' --include='*.html' --include='*.css' --include='*.js' "$out" || true)
    [ -z "$left" ] || die "a placeholder is left in: ${left//$out\//}"

    # The screenshots the pages name, from docs/images.
    mkdir -p "$out/images"
    while IFS= read -r name; do
        [ -n "$name" ] || continue
        [ -f "$tree/docs/images/$name.png" ] || die "a page shows images/$name.webp, and there is no docs/images/$name.png"
        magick "$tree/docs/images/$name.png" -strip -define webp:lossless=true -define webp:method=6 "$out/images/$name.webp"
    done < <(grep -rhoE 'images/[A-Za-z0-9_-]+\.webp' --include='*.html' --include='*.css' "$out" \
        | sed -e 's|^images/||' -e 's|\.webp$||' | sort -u)

    mkdir -p "$out/fonts"
    subset "$tree/app/fonts/NotoSans-Regular.ttf" "$out/fonts/noto-sans-400.woff2"
    subset "$tree/app/fonts/NotoSans-SemiBold.ttf" "$out/fonts/noto-sans-600.woff2"
    subset "$tree/app/fonts/NotoSans-Bold.ttf" "$out/fonts/noto-sans-700.woff2"
    # Without the ligatures, which would join the dashes of an option.
    subset "$tree/app/fonts/JetBrainsMono-Medium.ttf" "$out/fonts/jetbrains-mono-500.woff2" kern,mark,mkmk,ccmp,locl
    cp "$tree/LICENSES/OFL-1.1.txt" "$out/fonts/OFL-1.1.txt"

    cp "$tree/app/appicon/sc-apps-io.github.reverblabr.Reverblabr.svg" "$out/icon.svg"
    # The icon drawn for 16 pixels is the one for a browser's tab.
    cp "$tree/app/appicon/16-apps-io.github.reverblabr.Reverblabr.svg" "$out/favicon.svg"
    magick -background none -density 384 "$out/favicon.svg" -resize 32x32 -strip "$out/favicon-32.png"
    magick -background '#2a2e32' -density 288 "$out/icon.svg" -resize 180x180 -flatten -strip "$out/apple-touch-icon.png"

    # 1200 by 630, the size the link previews of most sites ask for: the icon,
    # the name and the summary on the dark window colour, and the canvas of the
    # demonstration session running off the right edge.
    magick -respect-parentheses -size 1200x630 'xc:#1b1e20' \
        \( -background none -density 576 "$out/icon.svg" -resize 112x112 \) -geometry +72+84 -composite \
        -font "$tree/app/fonts/NotoSans-Bold.ttf" -pointsize 72 -fill '#eff0f1' -annotate +68+300 'Reverblabr' \
        \( -size 460x -background none -font "$tree/app/fonts/NotoSans-Regular.ttf" -pointsize 34 \
        -fill '#c3c9ce' -interline-spacing 4 caption:'Capture, process and route audio on your desktop' \) \
        -geometry +72+332 -composite \
        -font "$tree/app/fonts/NotoSans-SemiBold.ttf" -pointsize 25 -fill '#f5a623' \
        -annotate +72+556 'For Linux, on KDE Plasma with PipeWire' \
        \( "$tree/docs/images/demo-session.png" -crop 1240x880+320+20 +repage -resize x540 \) \
        -geometry +590+45 -composite \
        -strip -quality 90 -sampling-factor 4:4:4 "$out/social.jpg"

    # A poster for each clip, so a page shows a picture before it plays.
    for clip in "$out"/media/*.webm; do
        [ -e "$clip" ] || continue
        ffmpeg -nostdin -loglevel error -sseof -4 -i "$clip" -frames:v 1 -y "$work/frame.png"
        magick "$work/frame.png" -strip -quality 82 -define webp:method=6 "${clip%.webm}-poster.webp"
    done

    : >"$out/.nojekyll"
}

# ---- the checks ----

check() {
    py - "$1" "$2" "$version" <<'EOF'
"""Every local link, image, clip and anchor of the built site resolves, and
every link into the repository at the version's tag names a file of the tree
given (and a heading of it, for an anchor). Nothing a page loads comes from
another site."""

import html.parser
import pathlib
import re
import sys
import urllib.parse

site, tree, version = pathlib.Path(sys.argv[1]).resolve(), pathlib.Path(sys.argv[2]), sys.argv[3]
SITE = "https://sorrelfox.github.io/reverblabr/"
REPO = "https://github.com/sorrelfox/reverblabr/"
AT_TAG = (f"{REPO}blob/v{version}/", f"{REPO}tree/v{version}/")
problems = []
checked = 0


class Page(html.parser.HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.refs, self.ids, self.base = [], set(), None

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        line = self.getpos()[0]
        if a.get("id"):
            self.ids.add(a["id"])
        # Nothing a page loads may come from another site: no fonts, scripts,
        # styles, pictures or clips from anywhere but the site itself.
        loads = [a.get("src"), a.get("poster")] + [part.split()[0] for part in (a.get("srcset") or "").split(",") if part.strip()]
        if tag == "link" and set((a.get("rel") or "").split()) & {"stylesheet", "icon", "preload", "apple-touch-icon", "manifest"}:
            loads.append(a.get("href"))
        for url in filter(None, loads):
            if urllib.parse.urlsplit(url).scheme or url.startswith("//"):
                problems.append(f"{self.name}:{line}: loads {url} from another site")
        if tag == "base":
            self.base = a.get("href")
        for key in ("href", "src", "poster"):
            if a.get(key):
                self.refs.append((line, a[key]))
        for part in (a.get("srcset") or "").split(","):
            if part.strip():
                self.refs.append((line, part.split()[0]))
        if tag == "meta" and a.get("property") in ("og:image", "og:url"):
            self.refs.append((line, a.get("content") or ""))
        if tag == "img" and "alt" not in a:
            problems.append(f"{self.name}:{line}: an image without alt")
        if tag in ("img", "video") and not (a.get("width") and a.get("height")):
            problems.append(f"{self.name}:{line}: an {tag} without width and height")


pages = {}
for path in sorted(site.rglob("*.html")):
    text = path.read_text(encoding="utf-8")
    page = Page()
    page.name = str(path.relative_to(site))
    page.feed(text)
    pages[path] = page
    for number, line in enumerate(text.splitlines(), 1):
        if "\u2014" in line:
            problems.append(f"{page.name}:{number}: an em-dash")


def slugs(markdown):
    """The anchors GitHub gives a Markdown file's headings."""
    found, seen, fenced = set(), {}, False
    for line in markdown.read_text(encoding="utf-8").splitlines():
        if line.startswith("```"):
            fenced = not fenced
            continue
        found.update(re.findall(r'<a (?:id|name)="([^"]+)"', line))
        heading = re.match(r"#{1,6}\s+(.*?)\s*#*\s*$", line)
        if fenced or not heading:
            continue
        words = re.sub(r"[`*_]|\[([^\]]*)\]\([^)]*\)", r"\1", heading.group(1)).strip().lower()
        slug = re.sub(r"[^\w\- ]", "", words).replace(" ", "-")
        count = seen.get(slug, 0)
        seen[slug] = count + 1
        found.add(slug if count == 0 else f"{slug}-{count}")
    return found


def local(where, target, fragment):
    global checked
    checked += 1
    target = target.resolve()
    if target.is_dir():
        target = target / "index.html"
    if not target.is_file():
        problems.append(f"{where}: no {target.relative_to(site) if site in target.parents else target}")
        return
    if fragment and target.suffix == ".html" and fragment not in pages[target].ids:
        problems.append(f"{where}: no #{fragment} in {target.relative_to(site)}")


for path, page in pages.items():
    for line, url in page.refs:
        where = f"{page.name}:{line}"
        parts = urllib.parse.urlsplit(url)
        fragment = urllib.parse.unquote(parts.fragment)
        if parts.scheme in ("mailto", "data"):
            continue
        if parts.scheme == "http":
            problems.append(f"{where}: plain http: {url}")
        elif url.startswith(SITE):
            local(where, site / urllib.parse.unquote(parts.path[len("/reverblabr/"):]), fragment)
        elif url.startswith(AT_TAG):
            checked += 1
            inside = urllib.parse.unquote(parts.path.split(f"/v{version}/", 1)[1])
            target = tree / inside
            if not target.exists():
                problems.append(f"{where}: the tree has no {inside}")
            elif fragment and target.suffix == ".md" and fragment not in slugs(target):
                problems.append(f"{where}: no heading #{fragment} in {inside}")
        elif url.startswith(REPO) and re.search(r"/(blob|tree)/", parts.path):
            problems.append(f"{where}: a link into the repository not at v{version}: {url}")
        elif parts.scheme:
            continue
        elif not parts.path:
            local(where, path, fragment)
        elif parts.path.startswith("/"):
            if not parts.path.startswith("/reverblabr/"):
                problems.append(f"{where}: an absolute path outside /reverblabr/: {url}")
            else:
                local(where, site / urllib.parse.unquote(parts.path[len("/reverblabr/"):]), fragment)
        else:
            base = site if page.base == "/reverblabr/" else path.parent
            local(where, (base / urllib.parse.unquote(parts.path)).resolve(), fragment)

for css in sorted(site.rglob("*.css")):
    for number, line in enumerate(css.read_text(encoding="utf-8").splitlines(), 1):
        if "@import" in line:
            problems.append(f"{css.relative_to(site)}:{number}: an @import")
        for url in re.findall(r"url\(\s*['\"]?([^'\")]+)['\"]?\s*\)", line):
            if urllib.parse.urlsplit(url).scheme or url.startswith("//"):
                problems.append(f"{css.relative_to(site)}:{number}: loads {url} from another site")
            else:
                local(f"{css.relative_to(site)}:{number}", (css.parent / url).resolve(), "")

for problem in problems:
    print(f"publish-website: {problem}", file=sys.stderr)
if problems:
    sys.exit(1)
print(f"publish-website: {len(pages)} pages, {checked} links and files checked")
EOF
}

# ---- preview, or a commit on gh-pages ----

if [ -n "$preview" ]; then
    mkdir -p "$preview"
    case "$preview/" in
    "$root/website/"* | "$root/")
        die "the preview can't go into the tree's own website/ or its top folder"
        ;;
    esac
    if [ -n "$(ls -A "$preview")" ]; then
        [ -f "$preview/.reverblabr-preview" ] || die "$preview is not empty and holds no earlier preview"
        find "$preview" -mindepth 1 -delete
    fi
    : >"$preview/.reverblabr-preview"
    build "$src" "$preview"
    check "$preview" "$linked"
    printf 'publish-website: the site of %s is in %s\n' "$version" "$preview"
    exit 0
fi

site=$work/site
mkdir -p "$site"
build "$src" "$site"
check "$site" "$linked"

name=$(git config user.name || true)
email=$(git config user.email || true)
[ -n "$name" ] && [ -n "$email" ] || die "this repository's git configuration has no user.name and user.email"

# The site's files, through an index of its own, into a tree object.
gitdir=$(git rev-parse --absolute-git-dir)
(cd "$site" && GIT_DIR=$gitdir GIT_WORK_TREE=$site GIT_INDEX_FILE=$work/site.index \
    git -c core.autocrlf=false -c core.fsmonitor=false add --all --force .)
tree=$(GIT_INDEX_FILE=$work/site.index git write-tree)

parent=$(git rev-parse -q --verify 'refs/heads/gh-pages^{commit}' || true)
if [ -n "$parent" ] && [ "$(git rev-parse "$parent^{tree}")" = "$tree" ]; then
    printf 'publish-website: gh-pages (%s) already holds this site; nothing to commit\n' \
        "$(git rev-parse --short "$parent")"
    exit 0
fi

head=$(git rev-parse HEAD)
short=$(git rev-parse --short HEAD)
message=$(printf 'Website for %s, from %s\n\nBuilt by tools/publish-website.sh from %s, "%s".\n\nSigned-off-by: %s <%s>' \
    "$version" "$short" "$head" "$(git log -1 --format=%s HEAD)" "$name" "$email")
commit=$(printf '%s\n' "$message" | git commit-tree "$tree" ${parent:+-p "$parent"})
git update-ref -m "publish-website: $version from $short" refs/heads/gh-pages "$commit" "$parent"

branch=$(git symbolic-ref -q --short HEAD || true)
remote=$(git config --get "branch.${branch:-dev}.remote" || true)
printf 'publish-website: gh-pages is now %s, the site of %s from %s.\n' "$(git rev-parse --short "$commit")" "$version" "$short"
printf 'Nothing was pushed. To publish it:\n  git push %s gh-pages\n' "${remote:-origin}"
printf 'GitHub serves it once Pages is set to deploy from the branch gh-pages, folder / (root).\n'
