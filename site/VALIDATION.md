# Website validation — 2026-11-07

The showcase was compared with the CyDAW reference in the browser before implementation. Product claims were checked against the 0.181 README, roadmap, public headers and existing desktop/view validation sources. The repository had no CyDoc HTML reader, and GitHub had no published release at inspection time. No existing documentation or application functionality was removed.

## Browser checks

The local static result was inspected through the in-app browser:

- English initial default; complete French page and guide, translated image captions/accessible labels and language-specific guide links.
- Desktop at 1280 × 720 and 1440 × 900, tablet at 1024 × 768, mobile at 390 × 844 and 320 × 720. No horizontal document overflow; GitHub remains visible in the header. The entire main capture is visible at these opening sizes.
- The screenshot dialog opens, displays the original full frame, responds to Escape and restores focus. The mobile dialog image fits within the viewport. Previous/next buttons and arrow-key navigation were exercised in the dialog.
- Mobile navigation opens/closes; selecting a link closes it. Review tabs select exactly one panel and respond to arrow keys. FAQ answers expand.
- French/English switching updates the URL without removing the current chapter, changes screenshot language and keeps guide destinations aligned with the selection.
- The command copy action completes and acknowledges the user gesture. Readable commands remain present as ordinary selectable text.
- The French guide initially exposed a horizontal mobile overflow; its grid/column minimum sizes were corrected and the rendered guide rechecked without overflow.
- Browser console checks returned no warnings/errors. Desktop and mobile visual captures are retained below ignored `build-site/`.

Direct `file://` navigation is blocked by this browser's protocol policy. No bypass was attempted. The guide was checked via local HTTP, and the bundle was audited for self-contained classic scripts, local data/styles and absence of CDN/fetch/module dependencies. Direct file opening remains the browser-verification limitation.

## Static/resource checks

`python site/tools/check.py --url <local-server>` passes 109 local resource/link/provenance checks. The ordinary public bundle passes 103 checks without an app archive. The shared source has 168 nonempty matching translation keys per language and 32 authentic original-fixture PNG captures, whose SHA-256 values match the public capture manifest.

The download preview archive is checked against the previously verified 0.181 SHA-256, including HTTP byte delivery. The default/public bundle includes no unpublished archive. Building once with the local archive and then without it was tested: the marked output directory is replaced and the unrequested archive is removed.

Styles and scripts use SHA-256-derived filenames, checked against their contents. All local page fragments, HTML links, images, styles and scripts resolve. `.nojekyll` and relative project paths are included for GitHub Pages. The initial local-only delivery performed no push, release creation or publication. The subsequent owner-authorized publication adds `.github/workflows/pages.yml`: pull requests build/check the public bundle, and `main` deploys it through GitHub Pages without the unpublished local application archive.

Screenshot contents are generated demonstration data, including a demo Git author; no real user project assets or Git history are shown. Website source and screenshots are intended for the repository's MIT distribution. The Windows application archive stays outside Git under ignored build output.

Gitleaks 8.30.1 reports zero findings across 564 publishable source files and all local refs. Text sources pass strict UTF-8/NUL checks and git diff --check.
