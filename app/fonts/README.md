<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Bundled typefaces

The Reverblabr appearance ([ADR-0012](../../docs/adr/0012-reverblabr-look.md))
draws its text in Noto Sans and its numbers in JetBrains Mono, the two faces
the design prototype names. They are compiled into the application as Qt
resources and registered at start (`Appearance::registerFonts()`), so the
window looks the same whether or not the machine has them installed. The
*Follow system* appearance does not use them.

| File | Family, weight | Version | Upstream |
|---|---|---|---|
| `NotoSans-Regular.ttf` | Noto Sans, 400 | 2.015 | <https://github.com/notofonts/latin-greek-cyrillic> |
| `NotoSans-Medium.ttf` | Noto Sans, 500 | 2.015 | 〃 |
| `NotoSans-SemiBold.ttf` | Noto Sans, 600 | 2.015 | 〃 |
| `NotoSans-Bold.ttf` | Noto Sans, 700 | 2.015 | 〃 |
| `JetBrainsMono-Medium.ttf` | JetBrains Mono, 500 | 2.304 | <https://github.com/JetBrains/JetBrainsMono> |
| `JetBrainsMono-SemiBold.ttf` | JetBrains Mono, 600 | 2.304 | 〃 |

These are the weights the prototype loads, and no others. Both are under the
SIL Open Font License 1.1 (`LICENSES/OFL-1.1.txt`), unmodified, with no
reserved font names; `REUSE.toml` carries their copyright lines.
