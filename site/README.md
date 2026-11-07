# CyUEDiff static website

This directory contains the English/French showcase and a bundled bilingual HTML guide. It uses original native desktop/CyUI screenshots generated from repository test fixtures. There is no framework, package installation, CDN, analytics or network dependency for the local resources. No CyDoc reader existed in this checkout; no existing documentation reader was replaced.

## Build a local preview

Requirements: Python 3.9+. No Unreal installation is needed to build the website.

```powershell
python site/tools/build.py
python site/tools/check.py
python -m http.server 8000 --bind 127.0.0.1 --directory build-site/pages
```

Open `http://127.0.0.1:8000/`. English is the initial default; `?lang=fr` selects French. The language selector retains the current fragment and updates guide links and screenshot language. The guide is at `guide.html?lang=fr#start` or `guide.html?lang=en#start`.

For the current **local-only** Windows download preview:

```powershell
python site/tools/build.py --include-local-build
```

This explicit option verifies the local archive against `build-native/morph-delivery-181.json`, copies it into the generated `downloads/` directory and labels it as a local preview build. It does not create a GitHub release. A normal build omits the archive and clearly says that no public release is available. The builder only replaces a marked, owned output directory below a workspace `build*` directory; rebuilding without this flag removes a previously included local archive.

## Files and maintenance

- `templates/index.html`: page structure and English fallback rendering.
- `templates/guide.html`: the separate HTML guide reader.
- `content.json`: complete English/French page copy, accessible labels and screenshot captions.
- `guide.json`: complete English/French guide chapters. It ships as local data, not fetched Markdown.
- `styles.css`, `app.js`: responsive layout, tabs, language controls, image dialog and keyboard handling.
- `assets/screenshots/`: authentic full-frame PNG captures in both languages, without added promotional overlays.
- `assets/capture-manifest.json`: original-fixture provenance and SHA-256 of each capture.
- `tools/build.py`: emits a ready-to-serve static bundle with content-hashed stylesheet/script URLs.
- `tools/check.py`: validates translations, resources, fragments, image provenance, archive integrity and optional local HTTP delivery.

The image dialog supports previous/next arrows and Escape, with focus returned to the original trigger. Tabs support arrow keys, Home and End. The GitHub link remains visible in the mobile header. The first desktop screen uses the full application capture with `object-fit: contain`, without cropping. Animations respect reduced-motion preferences.

The guide uses classic local scripts and local styles; it does not fetch content or require a server API. Keep the generated directory together for offline use. This task's browser permits only HTTP/HTTPS navigation, so direct `file://` opening could not be exercised there; the self-contained bundle and local HTTP guide were verified.

## Refresh screenshots

On a configured Windows Desktop/Git build:

```powershell
python site/tools/capture.py --build build-native --refresh-views
python site/tools/build.py --include-local-build
python site/tools/check.py --url http://127.0.0.1:8000
```

Capture generation uses the actual app's hidden WARP renderer and original encoded/in-memory test fixtures. Its owned temporary demo Git repository has a generic author and no real project content. Reports, sample packages, Git storage and capture intermediates stay below ignored `build-site/`; only curated PNGs and their public provenance manifest are tracked. Desktop captures show the application shell; component captures show actual embeddable CyUI views. Do not substitute private project captures without reviewing disclosure and redistribution rights.

## GitHub Pages preparation

`build-site/pages/` contains the complete static result: `index.html`, `guide.html`, local assets and `.nojekyll`. Paths are relative, so it works below a repository project URL. Styles and scripts receive new URLs whenever their bytes change. The default bundle contains no private build/archive paths or machine configuration.

For a public bundle without the unpublished local archive:

```powershell
python site/tools/build.py --output build-site/pages-public
python site/tools/check.py --output build-site/pages-public
```

This task intentionally adds no automatic deployment workflow and performs no push, release creation or Pages publication. Upload/deploy that directory only after the repository owner's explicit publication request. When releases become available, update the download metadata/copy using the actual published artifact URLs and checksums.

Original website code and demonstration assets follow the repository MIT license. Application third-party notices remain applicable to any included app archive.
