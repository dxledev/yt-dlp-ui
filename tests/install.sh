#!/usr/bin/env bash
set -euo pipefail
build="$1"
installer="$2"
root="$(/usr/bin/mktemp -d /tmp/ytdlp-install.XXXXXX)"
args=(--build-dir "$build" --prefix "$root/prefix" --config-dir "$root/config" --state-dir "$root/state" --apps-list "$root/apps.list" --no-palette)
/usr/bin/bash "$installer" "${args[@]}" --dry-run >/dev/null
[[ ! -e "$root/prefix" && ! -e "$root/apps.list" ]]
printf '%s\n' 'existing.desktop|icon|Existing app' > "$root/apps.list"
/usr/bin/bash "$installer" "${args[@]}" >/dev/null
/usr/bin/bash "$installer" "${args[@]}" >/dev/null
[[ "$(/usr/bin/awk -F'|' '$1 == "io.github.yt_dlp_ui.desktop" { count++ } END { print count }' "$root/apps.list")" == 1 ]]
[[ "$(/usr/bin/head -n1 "$root/apps.list")" == 'existing.desktop|icon|Existing app' ]]
/usr/bin/cmp "$build/yt-dlp-ui" "$root/prefix/bin/yt-dlp-ui"
[[ -L "$root/prefix/share/applications/io.github.yt_dlp_ui.desktop" ]]
/usr/bin/rg -q '^Name=Yt-Dlp$' "$root/prefix/bin/applications/io.github.yt_dlp_ui.desktop"
printf '%s\n' 'Desktop install, preserved app order, registration and dry-run passed.'
