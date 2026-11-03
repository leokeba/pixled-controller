#!/usr/bin/env bash
# Pins deps.lock and the siblings' reusable workflows to the commits: each
# one's HEAD in ../<repo>, or the commit given as repo=sha. Refuses a commit
# GitHub doesn't have, since CI fetches from there. With --check, fails if a
# workflow ref differs from deps.lock.
set -euo pipefail
cd "$(dirname "$0")/.."

lock=deps.lock
workflows=(.github/workflows/*.yml)
pins() { grep -E '^[^#[:space:]]+@[0-9a-f]{40}$' "$lock"; }

check() {
    local ok=0 repo sha line
    while IFS=@ read -r repo sha; do
        while IFS= read -r line; do
            if [ "${line##*@}" != "$sha" ]; then
                echo "$line: deps.lock pins ${repo#*/} $sha" >&2
                ok=1
            fi
        done < <(grep -HnE "^[[:space:]]*(-[[:space:]]*)?uses:[[:space:]]*$repo/" "${workflows[@]}" || true)
    done < <(pins)
    return "$ok"
}

if [ "${1:-}" = "--check" ]; then
    check && echo "Reusable workflow refs match deps.lock"
    exit
fi

out=$(mktemp)
grep -E '^#' "$lock" > "$out"
while IFS=@ read -r repo old; do
    name=${repo#*/}
    ref=HEAD
    for arg in "$@"; do [ "${arg%%=*}" = "$name" ] && ref=${arg#*=}; done
    dir=../$name
    sha=$(git -C "$dir" rev-parse --verify "$ref^{commit}")
    if [ -n "$(git -C "$dir" status --porcelain --untracked-files=no)" ] && [ "$ref" = HEAD ]; then
        echo "note: $dir has uncommitted changes; pinning its HEAD, $sha" >&2
    fi
    if ! git ls-remote "https://github.com/$repo.git" | grep -q "^$sha"; then
        # Not a branch or tag head: is it in main's history?
        git -C "$dir" fetch -q origin main
        if ! git -C "$dir" merge-base --is-ancestor "$sha" origin/main; then
            echo "$repo $sha isn't pushed: push it first" >&2
            rm -f "$out"
            exit 1
        fi
    fi
    [ "$sha" != "$old" ] && echo "$repo ${old:0:7} -> ${sha:0:7}"
    echo "$repo@$sha" >> "$out"
done < <(pins)
mv "$out" "$lock"

while IFS=@ read -r repo sha; do
    perl -pi -e "s{^(\s*(?:-\s*)?uses:\s*\Q$repo\E/[^@\s]+)\@\S+}{\$1\@$sha}" "${workflows[@]}"
done < <(pins)
check && echo "Reusable workflow refs match deps.lock"

kit=$(sed -n 's|^leokeba/TesserKIT@||p' "$lock")
kit_lock=$(git -C ../TesserKIT show "$kit:deps.lock" 2>/dev/null) || {
    echo "note: ../TesserKIT has no $kit to compare its deps.lock with" >&2
    exit 0
}
while IFS=@ read -r repo sha; do
    ours=$(sed -n "s|^$repo@||p" "$lock")
    if [ -n "$ours" ] && [ "$ours" != "$sha" ]; then
        echo "note: $repo pinned at ${ours:0:7}, TesserKIT ${kit:0:7} builds on ${sha:0:7}" >&2
    fi
done < <(grep -E '^[^#[:space:]]+@[0-9a-f]{40}$' <<< "$kit_lock")
