#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Fetches what the performance workflow compares a run with: the merged
# results of its last successful runs on the same branch, newest first, into
# DIR/1, DIR/2 and so on. The newest is the baseline and the others are the
# history the report shows beside it. A run that failed is never one of
# them, so a regression goes on failing until it is fixed or a baseline is
# chosen on purpose (tools/perf/baseline.json, which wins when it exists).
#
# Prints the folders it filled, one a line. Prints nothing, and succeeds,
# when there is nothing to fetch: the first run has no baseline.
#
# Usage: tools/perf/fetch-baseline.sh DIR [COUNT]
# Needs gh, with GH_TOKEN allowed to read the repository's actions.

set -euo pipefail

dir=${1:?usage: fetch-baseline.sh DIR [COUNT]}
count=${2:-5}
workflow=${RB_PERF_WORKFLOW:-perf.yml}
branch=${GITHUB_REF_NAME:-$(git rev-parse --abbrev-ref HEAD)}

if ! command -v gh > /dev/null; then
    echo "fetch-baseline: gh is not installed; no baseline" >&2
    exit 0
fi

mkdir -p "$dir"
index=0
for run in $(gh run list --workflow "$workflow" --branch "$branch" --status success \
    --limit "$count" --json databaseId --jq '.[].databaseId'); do
    index=$((index + 1))
    if gh run download "$run" --name perf-results --dir "$dir/$index" > /dev/null 2>&1 \
        && [ -f "$dir/$index/merged.json" ]; then
        echo "$dir/$index"
    else
        echo "fetch-baseline: run $run has no results to compare with" >&2
        rm -rf "${dir:?}/$index"
        index=$((index - 1))
    fi
done
