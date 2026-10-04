#!/usr/bin/env bash
set -euo pipefail
binary="$1"
project="$2"
root="$(/usr/bin/mktemp -d /tmp/ytdlp-integration.XXXXXX)"
export XDG_RUNTIME_DIR="$root/runtime"
/usr/bin/mkdir -m700 "$XDG_RUNTIME_DIR"
args=(--config-dir "$root/config" --state-dir "$root/state")
cli() { "$binary" "${args[@]}" "$@"; }
probe_status=0
/usr/bin/timeout 0.2 "$binary" "${args[@]}" --daemon > "$root/service-probe.log" 2>&1 || probe_status="$?"
if [[ "$probe_status" != 124 && "$probe_status" != 0 ]]; then
    if /usr/bin/rg -q 'QLocalServer::listen: Unknown error 1|Operation not permitted' "$root/service-probe.log"; then
        printf '%s\n' 'SKIP: this environment forbids creating local IPC sockets.'
        exit 77
    fi
    /usr/bin/cat "$root/service-probe.log" >&2
    exit 1
fi
cleanup() {
    cli pause >/dev/null 2>&1 || true
    while IFS= read -r id; do cli cancel "$id" >/dev/null 2>&1 || true; done < <(cli list 2>/dev/null | /usr/bin/jq -r '.[] | select(.status == "running" or .status == "queued") | .id')
    /usr/bin/sleep 0.1
    cli shutdown >/dev/null 2>&1 || true
}
trap cleanup EXIT
wait_status() {
    local id="$1" expected="$2"
    for (( attempt=0; attempt<100; ++attempt )); do
        if [[ "$(cli job "$id" | /usr/bin/jq -r .status)" == "$expected" ]]; then return; fi
        /usr/bin/sleep 0.05
    done
    cli job "$id" >&2
    exit 1
}
cli settings "binary=$project/tests/fake-yt-dlp" "dest=$root/downloads" >/dev/null
cli pause >/dev/null
cli add 'https://example.com/one' --dry-run | /usr/bin/jq -e '.arguments | index("--no-playlist") != null' >/dev/null
cli add 'https://example.com/one' --video-format webm --dry-run | /usr/bin/jq -e '.options.videoFormat == "webm" and (.arguments | index("--recode-video") != null)' >/dev/null
[[ "$(cli list | /usr/bin/jq length)" == 0 ]]
if cli add 'file:///etc/passwd' >/dev/null 2>&1; then exit 1; fi
first="$(cli add 'https://example.com/one' --quality 720 --video-format mp4 | /usr/bin/jq -r .id)"
second="$(cli add 'https://example.com/two' --mode audio --audio-format opus | /usr/bin/jq -r .id)"
discard="$(cli add 'https://example.com/discard' | /usr/bin/jq -r .id)"
cli remove "$discard" --dry-run >/dev/null
[[ "$(cli list | /usr/bin/jq length)" == 3 ]]
cli remove "$discard" >/dev/null
if cli job "$discard" >/dev/null 2>&1; then exit 1; fi
cli shutdown >/dev/null
cli status | /usr/bin/jq -e '.paused and (.jobs | length == 2)' >/dev/null
cli resume >/dev/null
wait_status "$first" done
wait_status "$second" done
cli job "$first" | /usr/bin/jq -e '.title == "Fixture media" and .file == "/tmp/fixture.mp4" and .percent == 100 and .videoFormat == "mp4"' >/dev/null
cli info 'https://example.com/one' | /usr/bin/jq -e '.title == "Fixture media" and (.formats | length == 1)' >/dev/null
cli info 'https://example.com/auth-required' --cookies-from-browser brave | /usr/bin/jq -e '.title == "Fixture media"' >/dev/null
cli settings cookiesFromBrowser=brave cookiesFile= >/dev/null
cli add 'https://example.com/auth-required' --dry-run | /usr/bin/jq -e '.arguments | index("--cookies-from-browser") != null' >/dev/null
authenticated="$(cli add 'https://example.com/auth-required' | /usr/bin/jq -r .id)"
wait_status "$authenticated" done
cli settings cookiesFromBrowser= >/dev/null
failed="$(cli add 'https://example.com/fail' | /usr/bin/jq -r .id)"
wait_status "$failed" failed
cli job "$failed" | /usr/bin/jq -e '.error | contains("fixture failure")' >/dev/null
slow="$(cli add 'https://example.com/slow' | /usr/bin/jq -r .id)"
wait_status "$slow" running
if cli remove "$slow" >/dev/null 2>&1; then exit 1; fi
cli cancel "$slow" --dry-run >/dev/null
[[ "$(cli job "$slow" | /usr/bin/jq -r .status)" == running ]]
cli cancel "$slow" >/dev/null
wait_status "$slow" cancelled
/usr/bin/sleep 0.1
cli retry "$first" >/dev/null
cli remove "$second" --dry-run >/dev/null
cli job "$second" >/dev/null
cli remove "$second" >/dev/null
if cli job "$second" >/dev/null 2>&1; then exit 1; fi
cli theme set 'accent=#123456' >/dev/null
for (( attempt=0; attempt<50; ++attempt )); do
    [[ "$(cli status | /usr/bin/jq -r '.theme.colors.accent')" == '#123456' ]] && break
    /usr/bin/sleep 0.05
done
[[ "$(cli status | /usr/bin/jq -r '.theme.colors.accent')" == '#123456' ]]
cli theme set 'accent=#abcdef' --dry-run >/dev/null
[[ "$(cli theme show | /usr/bin/jq -r '.colors.accent')" == '#123456' ]]
if cli clear >/dev/null 2>&1; then exit 1; fi
cli pause >/dev/null
cli clear --yes --dry-run >/dev/null
[[ "$(cli list | /usr/bin/jq length)" -gt 0 ]]
/usr/bin/timeout 1 "$binary" "${args[@]}" watch > "$root/events.jsonl" || [[ "$?" == 124 ]]
/usr/bin/jq -e 'select(.event == "snapshot") | .data.version == 1' "$root/events.jsonl" >/dev/null
cli clear --yes >/dev/null
printf '%s\n' 'Queue persistence, structured progress, metadata, cancellation, dry-run, theme IPC and subscriptions passed.'
