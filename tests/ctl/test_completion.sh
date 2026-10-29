#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# reverblabr-ctl's shell completions and man page, without a bus: a
# stand-in reverblabr-ctl on PATH answers `list` with names that have
# spaces and Unicode in them, and records how it was asked.
#
#   test_completion.sh <ctl source folder>

# Not -e: completion functions run in a shell without errexit, as
# bash-completion runs them, and an arithmetic step that counts from nought
# is false.
set -uo pipefail
ctl_dir=$1
failures=0
fail() {
    printf 'FAIL: %s\n' "$*"
    failures=$((failures + 1))
}

scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
cat > "$scratch/reverblabr-ctl" <<'STANDIN'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "${0%/*}/asked"
printf '%s\n' "Evening radio" "Podcast — Mic + Call ☕" "Talk"
STANDIN
chmod +x "$scratch/reverblabr-ctl"
PATH="$scratch:$PATH"

# --- bash ---------------------------------------------------------------
# shellcheck source=/dev/null
source "$ctl_dir/completions/reverblabr-ctl.bash"

complete_words() {
    COMP_WORDS=("$@")
    COMP_CWORD=$((${#COMP_WORDS[@]} - 1))
    COMPREPLY=()
    _reverblabr_ctl
}

complete_words reverblabr-ctl st
[[ " ${COMPREPLY[*]} " == *" start "* && " ${COMPREPLY[*]} " == *" status "* && " ${COMPREPLY[*]} " == *" stop "* ]] ||
    fail "bash: 'st' offered '${COMPREPLY[*]}', not start, status and stop"

complete_words reverblabr-ctl --js
[[ ${COMPREPLY[*]} == "--json" ]] || fail "bash: '--js' offered '${COMPREPLY[*]}'"

complete_words reverblabr-ctl --json start P
[[ ${#COMPREPLY[@]} -eq 1 ]] || fail "bash: 'start P' offered ${#COMPREPLY[@]} names"
# One word, quoted so the shell reads it back as the whole name.
eval "set -- ${COMPREPLY[0]}"
[[ $# -eq 1 && $1 == "Podcast — Mic + Call ☕" ]] || fail "bash: the name came back as $# words: '$*'"

complete_words reverblabr-ctl start '"Evening r'
[[ ${#COMPREPLY[@]} -eq 1 ]] || fail "bash: a name begun in quotes offered ${#COMPREPLY[@]} names"

complete_words reverblabr-ctl switch Talk ""
[[ " ${COMPREPLY[*]} " == *" A "* && " ${COMPREPLY[*]} " == *" B "* ]] || fail "bash: switch offered '${COMPREPLY[*]}', not A and B"

# **Tab never starts the engine**: every question it asked said so.
if grep -qv -- '--no-activate' "$scratch/asked"; then
    fail "bash: completion asked without --no-activate: $(grep -v -- '--no-activate' "$scratch/asked")"
fi

# What is typed is never run by pressing Tab.
: > "$scratch/ran"
complete_words reverblabr-ctl start "\$(touch $scratch/ran-by-tab)"
[[ ! -e $scratch/ran-by-tab ]] || fail "bash: completion ran what was typed"

# --- zsh ----------------------------------------------------------------
if command -v zsh > /dev/null; then
    zsh -n "$ctl_dir/completions/_reverblabr-ctl" || fail "zsh: the completion does not parse"
    head -1 "$ctl_dir/completions/_reverblabr-ctl" | grep -q '^#compdef reverblabr-ctl$' ||
        fail "zsh: the completion does not begin with #compdef reverblabr-ctl"
    grep -q -- '--no-activate' "$ctl_dir/completions/_reverblabr-ctl" || fail "zsh: completion may start the engine"
else
    echo "zsh not installed; its completion not checked"
fi

# --- the man page -------------------------------------------------------
if command -v groff > /dev/null; then
    warnings=$(groff -man -ww -z "$ctl_dir/reverblabr-ctl.1" 2>&1) || true
    [[ -z $warnings ]] || fail "man page: $warnings"
else
    echo "groff not installed; the man page not checked"
fi
for code in 0 1 2 3 4 5; do
    grep -q "^\.B $code$" "$ctl_dir/reverblabr-ctl.1" || fail "man page: exit status $code is not described"
done

if ((failures)); then
    exit 1
fi
echo "PASS: completion and manual"
