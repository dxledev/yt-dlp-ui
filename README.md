# Yt-Dlp

A portable, standalone native client for [yt-dlp](https://github.com/yt-dlp/yt-dlp). Paste a link, choose video or audio, and let the queue handle the rest. Built with C++20 and Qt Widgets: solid colors, a resizable Wayland window, and no browser runtime.

The queue workflow is inspired by [Alex Zeitler’s download queue](https://github.com/AlexZeitler/omarchy-yt-dlp-plugin). This is an independent native implementation.

- Sequential downloads with titles, percentage, speed, ETA, cancellation and retry.
- Best video quality or a height limit; Auto, MP4, MKV or WebM video output; original audio, MP3, M4A, Opus, FLAC or WAV.
- Persistent queue and settings. Downloads continue when the window closes.
- In-window settings, folder picker, media information and download details.
- Live, persistent JSON themes controlled through the CLI.
- JSON commands and event subscriptions for other applications.
- Optional browser cookies or Netscape cookie files for sites that require sign-in.

## Build and run

Requires Linux, CMake 3.21+, a C++20 compiler, Qt 6.7+ (Core, Gui, Widgets, Network), yt-dlp, and ffmpeg for merging/conversion. Qt Test is needed when tests are enabled. Install the Qt Wayland platform plugin for Wayland.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build -j 4
./build/yt-dlp-ui
```

The executable is called `yt-dlp-ui`; the application is **Yt-Dlp**.

For a personal desktop installation:

```bash
./scripts/install-desktop.sh --dry-run
./scripts/install-desktop.sh
```

The installer places the binary in `~/.local/bin`, the desktop entry in `~/.local/bin/applications`, and its registration in `~/.local/share/applications`. It appends an entry to `~/.config/apps.list` without changing existing order. Existing registration is deduplicated; modified registration files are backed up. Installation paths, palette source and state directories are configurable; see `--help`. Use `--no-palette` for an independent initial palette, or `--palette /absolute/path/to/Colors.qml` to follow your own palette. Existing saved themes are preserved.

A standard installation is also available:

```bash
cmake --install build --prefix "$HOME/.local"
```

Keep yt-dlp up to date through your package manager or its own installation method. Settings accepts a different yt-dlp executable, including an executable wrapper for a source checkout. No downloads, clones or updates happen automatically.

## Theme from the CLI

Colors live in `${XDG_CONFIG_HOME:-~/.config}/yt-dlp-ui/theme.json`. Theme commands atomically edit that JSON file immediately. The running client reloads the palette without a restart. Invalid or temporarily unavailable palettes keep the last good colors.

```bash
yt-dlp-ui theme presets
yt-dlp-ui theme preset slate
yt-dlp-ui theme set 'background=#131720' 'accent=#b2c7ff'
yt-dlp-ui theme import /path/to/palette.json
yt-dlp-ui theme follow file /path/to/Colors.qml
yt-dlp-ui theme follow file /path/to/palette-directory
yt-dlp-ui theme unfollow
yt-dlp-ui theme export /path/to/my-theme.json
```

Color roles: `background`, `surface`, `surface_alt`, `text`, `muted`, `accent`, `accent_text`, `border`, `danger`, `success`. Run `theme keys` for JSON output. Presets are `slate`, `forest`, `paper`, and `rose`. All colors use `#RRGGBB`.

A native theme file has this shape:

```json
{
  "version": 1,
  "name": "My palette",
  "colors": {
    "background": "#131720",
    "surface": "#1c2230",
    "surface_alt": "#252e40",
    "text": "#ebeffa",
    "muted": "#97a3be",
    "accent": "#b2c7ff",
    "accent_text": "#1d2a4b",
    "border": "#34405a",
    "danger": "#f18c93",
    "success": "#b2c7ff"
  }
}
```

Imports also support plain JSON colors, `colors`/`colours` objects, material palette aliases, and QML `property color` declarations with hex values or literal `Qt.rgba(...)` channels. A directory is searched for `theme.json`, `palette.json`, `Colors.qml`, then `colors.json`. Following observes both the logical path and its resolved target, including atomic replacement and symlink changes. Imported colors are saved in the application’s JSON so the last palette survives a missing source. A direct `theme set`, `preset`, or `import` switches to independent colors; `follow` resumes source tracking.

The headless service keeps a followed palette synchronized after it has been started, even while the window is closed. Run `yt-dlp-ui status` to start it without opening a window.

## Queue and IPC

```bash
yt-dlp-ui add 'https://example.com/video' --quality 1080 --dest ~/Downloads
yt-dlp-ui add 'https://example.com/video' --video-format mp4
yt-dlp-ui add 'https://example.com/video' --mode audio --audio-format opus
yt-dlp-ui list
yt-dlp-ui status
yt-dlp-ui job JOB_ID
yt-dlp-ui info 'https://example.com/video'
yt-dlp-ui cancel JOB_ID
yt-dlp-ui retry JOB_ID
yt-dlp-ui remove JOB_ID --dry-run
yt-dlp-ui remove JOB_ID
yt-dlp-ui pause
yt-dlp-ui resume
yt-dlp-ui watch
yt-dlp-ui settings 'binary=/usr/bin/yt-dlp' 'maxHistory=200'
yt-dlp-ui clear --yes
yt-dlp-ui shutdown
```

Commands return JSON. `watch` streams newline-delimited JSON events for queue, progress and theme updates. Other applications can call these commands directly; no GUI or display connection is needed. `job` returns the selected options, status, progress, metadata, output file, timestamps, error and bounded log. `info` returns yt-dlp’s full media JSON without downloading. See [the IPC protocol](docs/ipc.md) for socket clients.

`pause` stops dispatching new jobs; the active job continues. `cancel` also terminates conversion subprocesses belonging to that job. Each queue entry has a trash icon; `remove JOB_ID` performs the same action from the CLI. Removal forgets one queued or finished entry and keeps its downloaded files. Cancel an active download and wait for it to stop before removing it. `clear --yes` forgets all finished history and also keeps downloaded files. `shutdown` requires an idle service. Use `--dry-run` on mutation commands to preview changes. Downloads use `--no-playlist`: one submitted link downloads one item. Video and audio streams can report separate percentages.

The video format dropdown is independent of quality. **Auto** keeps yt-dlp’s chosen output container. **MP4**, **MKV** and **WebM** produce the selected format, using ffmpeg conversion when needed. Conversion can take additional time. The selection is saved as the `videoFormat` default and in each job; retry retains that job’s choice. Set it through `settings videoFormat=mp4` or override it with `add --video-format mp4`. Audio filenames include an `(audio)` suffix and explicit video formats include a corresponding suffix such as `(webm)`, keeping conversion outputs separate from other versions of the same download.

## Files and resource use

The window and the headless service use separate processes. CLI operations use `QCoreApplication` and need no graphical platform. The UI uses native widgets and a painted list delegate, with no web engine, thumbnail cache, or permanent polling loop. Updates arrive over a local socket. Progress writes are coalesced, history is bounded (200 finished jobs by default, with a 4 MiB state budget), and logs are bounded per job. yt-dlp and ffmpeg are child processes and their resource use is separate from the client.

Default files:

- Config: `${XDG_CONFIG_HOME:-~/.config}/yt-dlp-ui/{theme,settings,window}.json`
- Queue: `${XDG_STATE_HOME:-~/.local/state}/yt-dlp-ui/queue.json`
- IPC: a user-only local socket in `$XDG_RUNTIME_DIR`, or the private state directory.

Use `--config-dir DIR` and `--state-dir DIR`, or `YT_DLP_UI_CONFIG_DIR` and `YT_DLP_UI_STATE_DIR`, for independent profiles. All processes for a profile must use the same directories. The queue resumes interrupted downloads after an unexpected service exit, allowing yt-dlp’s normal partial-file continuation.

Keyboard shortcuts: `Ctrl+L` focuses the link, `Ctrl+Enter` adds a download, `Ctrl+,` opens settings, and `Escape` dismisses a modal.

## Sign-in and cookies

If yt-dlp reports “Please sign in” or asks you to confirm you are not a bot, open **Settings → Sign-in cookies**, select the browser where you are signed into the site, save, and retry the failed download. For example, select **Brave** if you use Brave to watch YouTube. Browser/profile options can include `brave:Profile 1` or yt-dlp’s full `BROWSER[+KEYRING][:PROFILE][::CONTAINER]` syntax. You can also choose a Netscape cookie file.

The same settings are available through the CLI:

```bash
yt-dlp-ui settings cookiesFromBrowser=brave cookiesFile=
yt-dlp-ui info 'https://example.com/video' --cookies-from-browser brave
yt-dlp-ui add 'https://example.com/video' --cookies-from-browser brave
yt-dlp-ui add 'https://example.com/video' --cookies /path/to/cookies.txt
yt-dlp-ui add 'https://example.com/video' --no-cookies
```

Cookie support starts disabled. The application stores the selected browser/profile or cookie-file path in its JSON, and yt-dlp reads the chosen source when downloading or inspecting media. Choose one source. Changing the default affects new downloads and Info requests; **Retry** uses the current cookie settings while keeping the original link, destination and format choices. Jobs already waiting retain the cookie settings they were added with.

Yt-Dlp passes these options explicitly because it runs yt-dlp with `--ignore-config`. Cookie settings in a separate yt-dlp configuration file are therefore not inherited. See the [official cookie instructions](https://github.com/yt-dlp/yt-dlp/wiki/FAQ#how-do-i-pass-cookies-to-yt-dlp). Authentication must belong to an account that can view the video; cookie support does not grant access by itself.

## Verify

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

Tests cover input validation, atomic palette edits, symlink retargeting, invalid-palette recovery, embedded modal behavior, queue persistence, progress, cancellation, metadata, CLI events, and installation into temporary directories. Integration tests use a local fixture executable. Worker tests also run real yt-dlp video downloads and ffmpeg audio conversion against a generated local media file, without internet access. Environments that prohibit creating Unix sockets skip the end-to-end IPC test.
