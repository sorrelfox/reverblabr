<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Contributing to Reverblabr

Thanks for your interest. Reverblabr is a project I work on in my free time, so please be patient with me: reviews and answers can take a while.

## Reporting bugs

Open an issue on GitHub and use the bug template. Most useful are the steps that make the problem happen again, and the text **Copy Diagnostics** puts on the clipboard (the Info tab, under *Audio system*). It has the versions, how each session is keeping up, the audio graph around Reverblabr and its last log lines. [When something goes wrong](docs/troubleshooting.md) has more.

## Changing the code

- **Building and testing:** [docs/development.md](docs/development.md)
- **How it's put together:** [docs/architecture.md](docs/architecture.md) and the [decision records](docs/adr/README.md)
- **Coding conventions** (naming, file layout, error handling, the real-time rules, Qt and QML, tests, commit messages): [docs/conventions.md](docs/conventions.md)

Development happens on the `dev` branch, and `main` only gets releases, so please base your work on `dev` and open pull requests against it. For anything bigger than a small fix, open an issue first and describe what you have in mind, so we can agree on the approach before you spend time on it.

CI doesn't run on pull requests: it is started by hand before a release, to save the project's limited GitHub Actions minutes. Before you open a pull request, please run the checks listed in [Before a change goes in](docs/development.md#before-a-change-goes-in) and say in the description what you ran.

## Signing off your commits

Every commit needs a `Signed-off-by:` line with your name and e-mail address:

```
Signed-off-by: Your Name <you@example.org>
```

`git commit -s` adds it. By signing off you certify the [Developer Certificate of Origin 1.1](https://developercertificate.org/): you wrote the change, or otherwise have the right to submit it under the project's licences. There is no contributor licence agreement and no copyright assignment. Your contribution is licensed under the licence of the files it changes.

## Licences

The project follows the [REUSE](https://reuse.software) specification. Every file names its licence in an SPDX header or, where a file can't carry one, in [`REUSE.toml`](REUSE.toml). `reuse lint` must pass. The full texts are in [`LICENSES/`](LICENSES/).

| Content | Licence |
|---|---|
| Source code: C++, QML, CMake, scripts | MPL-2.0, with the header shown in [the conventions](docs/conventions.md#files) |
| Public interfaces: D-Bus interface XML, SDK headers | MIT |
| Documentation and artwork | CC-BY-SA-4.0 |
| Metadata, presets, templates, configuration snippets, test audio | CC0-1.0 |

The reasons are in [ADR-0005](docs/adr/0005-licence-map.md). The name and logo are covered by [TRADEMARKS.md](TRADEMARKS.md), and third-party components are listed in [CREDITS.md](CREDITS.md); a change that adds a dependency updates that file too.

## Write it yourself

Reverblabr is an original work. Reimplementing a feature you know from another program is fine; copying someone else's expression is not.

- Don't copy anything from another product into the repository, a commit, an issue or a pull request: text, names of features or products, icons, screenshots, sounds, documentation or other assets.
- Write in your own words. When a well-known feature has a brand-like name, pick a neutral, descriptive one instead.
- Keep research material outside the repository, and don't quote it.
- The git hooks in [`tools/hooks/`](tools/hooks/) can catch accidents for you; [docs/development.md](docs/development.md#git-hooks) explains how to turn them on.
- If you aren't sure whether something counts as copying, ask in the issue before writing it.

## What CI checks

Before a release, GitHub Actions checks the sign-offs, the clean-room rule, `reuse lint` and `clang-format`, then builds on Fedora 44 with warnings as errors, runs the whole test suite and treats any qmllint warning as an error.
