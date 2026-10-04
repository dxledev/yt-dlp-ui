#!/usr/bin/env bash
set -euo pipefail

project_directory="$(cd -- "${BASH_SOURCE[0]%/*}/.." && pwd)"
build_directory="$project_directory/build"
install_prefix="$HOME/.local"
config_directory="${XDG_CONFIG_HOME:-$HOME/.config}/yt-dlp-ui"
state_directory="${XDG_STATE_HOME:-$HOME/.local/state}/yt-dlp-ui"
apps_list="${XDG_CONFIG_HOME:-$HOME/.config}/apps.list"
palette="${XDG_CONFIG_HOME:-$HOME/.config}/themes/.caelestia-use/Colors.qml"
desktop_id=io.github.yt_dlp_ui
dry_run=0

usage() {
    printf '%s\n' 'Usage: install-desktop.sh [--dry-run] [--build-dir DIR] [--prefix DIR]' \
        '  [--config-dir DIR] [--state-dir DIR] [--apps-list FILE] [--palette FILE_OR_DIR|--no-palette]'
}
fail() { printf 'install-desktop: %s\n' "$*" >&2; exit 1; }
parse_options() {
    while (( $# )); do
        case "$1" in
            --dry-run) dry_run=1; shift ;;
            --help|-h) usage; exit 0 ;;
            --no-palette) palette=""; shift ;;
            --build-dir|--prefix|--config-dir|--state-dir|--apps-list|--palette)
                (( $# >= 2 )) || fail "Missing value for $1"
                [[ "$2" == /* && "$2" != / && "$2" != *$'\n'* && "$2" != *'|'* && "$2" != *'"'* && "$2" != *'%'* && "$2" != *'\\'* ]] || fail "Expected a safe absolute path for $1"
                case "$1" in
                    --build-dir) build_directory="${2%/}" ;;
                    --prefix) install_prefix="${2%/}" ;;
                    --config-dir) config_directory="${2%/}" ;;
                    --state-dir) state_directory="${2%/}" ;;
                    --apps-list) apps_list="$2" ;;
                    --palette) palette="${2%/}" ;;
                esac
                shift 2 ;;
            *) fail "Unknown option: $1" ;;
        esac
    done
}
validate() {
    [[ -x "$build_directory/yt-dlp-ui" ]] || fail "Build first: $build_directory/yt-dlp-ui"
    [[ ! -L "$apps_list" && ( ! -e "$apps_list" || -f "$apps_list" ) ]] || fail "App list must be a regular file: $apps_list"
    [[ ! -L "$install_prefix/bin/yt-dlp-ui" ]] || fail 'Existing executable is a symlink'
    local desktop="$install_prefix/bin/applications/$desktop_id.desktop"
    local registered="$install_prefix/share/applications/$desktop_id.desktop"
    [[ ! -L "$desktop" ]] || fail 'Existing desktop entry is a symlink'
    [[ ! -e "$registered" && ! -L "$registered" || -L "$registered" && "$(/usr/bin/readlink -- "$registered")" == "$desktop" ]] || fail "Unmanaged desktop registration: $registered"
    [[ -e "$palette" ]] || palette=""
    if [[ -n "$palette" ]]; then
        "$build_directory/yt-dlp-ui" --config-dir "$config_directory" --state-dir "$state_directory" theme follow file "$palette" --dry-run >/dev/null
    fi
}
backup() {
    [[ -f "$1" ]] || return 0
    /usr/bin/cp -p -- "$1" "$1.ytdlp-backup.$(/usr/bin/date +%Y%m%dT%H%M%S).$$"
}
install_files() {
    local desktop="$install_prefix/bin/applications/$desktop_id.desktop"
    local registered="$install_prefix/share/applications/$desktop_id.desktop"
    local icon="$install_prefix/share/icons/hicolor/scalable/apps/$desktop_id.svg"
    /usr/bin/mkdir -p -- "$install_prefix/bin" "${desktop%/*}" "${registered%/*}" "$config_directory" "$state_directory"
    /usr/bin/chmod 700 -- "$state_directory"
    local temporary
    temporary="$(/usr/bin/mktemp "$install_prefix/bin/.yt-dlp-ui.XXXXXX")"
    /usr/bin/install -m755 -- "$build_directory/yt-dlp-ui" "$temporary"
    /usr/bin/mv -T -- "$temporary" "$install_prefix/bin/yt-dlp-ui"
    /usr/bin/install -Dm644 -- "$project_directory/assets/$desktop_id.svg" "$icon"
    backup "$desktop"
    temporary="$(/usr/bin/mktemp "${desktop%/*}/.ytdlp-desktop.XXXXXX")"
    /usr/bin/awk -v executable="$install_prefix/bin/yt-dlp-ui" -v config="$config_directory" -v state="$state_directory" \
        '/^Exec=/ { print "Exec=\"" executable "\" --config-dir \"" config "\" --state-dir \"" state "\""; next } { print }' \
        "$project_directory/assets/$desktop_id.desktop" > "$temporary"
    /usr/bin/chmod 644 -- "$temporary"
    /usr/bin/mv -T -- "$temporary" "$desktop"
    [[ -L "$registered" ]] || /usr/bin/ln -s -- "$desktop" "$registered"
    if [[ -x /usr/bin/desktop-file-validate ]]; then /usr/bin/desktop-file-validate "$desktop"; fi
    if [[ -x /usr/bin/update-desktop-database ]]; then /usr/bin/update-desktop-database "${registered%/*}"; fi
}
register_app() {
    if [[ -f "$apps_list" ]] && /usr/bin/awk -F '|' -v id="$desktop_id.desktop" '$1 == id { found = 1 } END { exit !found }' "$apps_list"; then return; fi
    /usr/bin/mkdir -p -- "${apps_list%/*}"; backup "$apps_list"
    local temporary
    temporary="$(/usr/bin/mktemp "${apps_list%/*}/.ytdlp-apps.XXXXXX")"
    if [[ -f "$apps_list" ]]; then /usr/bin/awk '{ print }' "$apps_list" > "$temporary"; /usr/bin/chmod --reference="$apps_list" "$temporary"; fi
    printf '%s\n' "$desktop_id.desktop|$install_prefix/share/icons/hicolor/scalable/apps/$desktop_id.svg|Yt-Dlp" >> "$temporary"
    /usr/bin/mv -T -- "$temporary" "$apps_list"
}
configure_theme() {
    if [[ ! -f "$config_directory/theme.json" && -n "$palette" ]]; then
        "$install_prefix/bin/yt-dlp-ui" --config-dir "$config_directory" --state-dir "$state_directory" theme follow file "$palette" >/dev/null
    fi
}
main() {
    parse_options "$@"; validate
    if (( dry_run )); then
        printf 'Executable: %s/bin/yt-dlp-ui\nDesktop: %s/bin/applications/%s.desktop\nRegistration: %s/share/applications/%s.desktop\nApp list: %s\nPalette: %s\nConfig: %s\nState: %s\n' \
            "$install_prefix" "$install_prefix" "$desktop_id" "$install_prefix" "$desktop_id" "$apps_list" "$palette" "$config_directory" "$state_directory"
        return
    fi
    install_files; register_app; configure_theme
    printf 'Installed Yt-Dlp.\n'
}
main "$@"
