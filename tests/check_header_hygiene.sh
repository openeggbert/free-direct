#!/usr/bin/env bash
# 24-Hour Stabilization Backlog TASK-24H-0024 (plan.md).
#
# Enforces CLAUDE.md's Internal Backend Policy: no SDL3/SDL3_net/ENet identifier may ever appear
# in a public header under include/. English prose in a Doxygen comment describing behavior (e.g.
# "feeds PCM data into SDL_AudioStream") is not a violation - only a real leaking symbol (an
# #include, a type, a declaration) on an actual code line is. A line is treated as a comment line,
# and therefore allowed, if - after stripping leading whitespace - it starts with "*", "/**", or
# "//" (matching this codebase's existing Doxygen comment style throughout include/*.h).
set -euo pipefail

INCLUDE_DIR="${1:?usage: check_header_hygiene.sh <include-dir>}"

violations=0
while IFS= read -r line; do
    [ -z "$line" ] && continue
    file="${line%%:*}"
    rest="${line#*:}"
    lineno="${rest%%:*}"
    content="${rest#*:}"
    trimmed="$(printf '%s' "$content" | sed -e 's/^[[:space:]]*//')"
    if printf '%s' "$trimmed" | grep -qE '^(\*|/\*\*|//)'; then
        continue # comment line - prose mention allowed
    fi
    echo "Header hygiene violation: $file:$lineno:$content"
    violations=$((violations + 1))
done < <(grep -rniE "sdl_|sdl3_net|enet" "$INCLUDE_DIR" || true)

if [ "$violations" -gt 0 ]; then
    echo "$violations header hygiene violation(s) found under $INCLUDE_DIR"
    exit 1
fi
echo "Header hygiene OK: no SDL/ENet identifiers found outside comments under $INCLUDE_DIR"
exit 0
