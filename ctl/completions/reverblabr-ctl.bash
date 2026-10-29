# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# bash completion for reverblabr-ctl: the commands and options, and the
# sessions' names. Names are asked of an engine that is already running
# (--no-activate): pressing Tab never starts the engine.

_reverblabr_ctl_sessions()
{
    local names
    names=$(reverblabr-ctl --no-activate --timeout 2 list 2>/dev/null) || return
    local IFS=$'\n'
    local name
    for name in $names; do
        # Names have spaces in them; each is offered as one word.
        if [[ $name == "$cur"* ]]; then
            COMPREPLY+=("$(printf '%q' "$name")")
        fi
    done
}

# The timers' ids: each line of `timers list` ends with one, in brackets.
_reverblabr_ctl_timers()
{
    local ids
    ids=$(reverblabr-ctl --no-activate --timeout 2 timers list 2>/dev/null |
        sed -n 's/.*(\([A-Za-z0-9_-]*\))$/\1/p') || return
    mapfile -t COMPREPLY < <(compgen -W "$ids" -- "$cur")
}

_reverblabr_ctl()
{
    local cur
    cur=${COMP_WORDS[COMP_CWORD]}
    # The word typed so far, without its opening quote and backslashes, so a
    # name with a space matches. Never eval'd: what is typed on a command
    # line is not to be run by pressing Tab.
    cur=${cur#[\"\']}
    cur=${cur//\\/}
    COMPREPLY=()

    local commands="list status start stop split switch set timers timer-fire"
    local options="--json --timeout --no-activate --help --version"

    # The command is the first word after the options.
    local index command="" position=0
    for ((index = 1; index < COMP_CWORD; index++)); do
        local word=${COMP_WORDS[index]}
        if [[ $word == --timeout ]]; then
            index=$((index + 1))
            continue
        fi
        [[ $word == -* ]] && continue
        if [[ -z $command ]]; then
            command=$word
        else
            position=$((position + 1))
        fi
    done

    if [[ $cur == -* ]]; then
        mapfile -t COMPREPLY < <(compgen -W "$options" -- "$cur")
        return
    fi
    if [[ -z $command ]]; then
        mapfile -t COMPREPLY < <(compgen -W "$commands" -- "$cur")
        return
    fi
    case $command in
    status | start | stop | split | switch | set)
        if ((position == 0)); then
            _reverblabr_ctl_sessions
        elif [[ $command == switch ]]; then
            mapfile -t COMPREPLY < <(compgen -W "A B" -- "$cur")
        fi
        ;;
    timers)
        if ((position == 0)); then
            mapfile -t COMPREPLY < <(compgen -W "list" -- "$cur")
        elif ((position == 1)); then
            _reverblabr_ctl_sessions
        fi
        ;;
    timer-fire)
        ((position == 0)) && _reverblabr_ctl_timers
        ;;
    esac
}

complete -F _reverblabr_ctl reverblabr-ctl
