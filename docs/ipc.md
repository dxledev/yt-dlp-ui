# IPC protocol

The CLI is the supported automation interface. For example, `yt-dlp-ui status` prints one JSON object, `list` prints an array, and `watch` streams event objects. Failed commands exit nonzero and print a message to stderr.

The CLI starts a headless service on demand. Each profile owns a user-only Unix socket whose name is derived from its absolute config and state paths. The name is `yt-dlp-ui-<first 16 hex characters of SHA256(config + newline + state)>.sock`, under `$XDG_RUNTIME_DIR` (or the state directory if no runtime directory exists). A lock protects service startup; independent profiles have independent services.

Socket clients send one JSON object per line:

```json
{"id":1,"command":"add","options":{"url":"https://example.com/video","mode":"video","quality":"1080"}}
```

Responses echo the request ID:

```json
{"id":1,"ok":true,"data":{"id":"job-uuid","status":"queued"}}
```

```json
{"id":1,"ok":false,"error":"Enter a valid http or https link"}
```

The available commands are `status`, `list`, `job`, `add`, `cancel`, `retry`, `remove`, `pause`, `resume`, `clear`, `settings`, `info`, `show`, `shutdown`, and `subscribe`. Theme commands edit the persistent JSON directly; subscribed clients receive the resulting theme in a snapshot event.

| Command | Options | Result |
| --- | --- | --- |
| `status` | none | Profile snapshot |
| `list` | none | Job array, oldest first |
| `job` | `id` | Job object |
| `add` | `url`, optional `mode`, `quality`, `videoFormat`, `audioFormat`, `dest`, `binary`, `cookiesFromBrowser`, `cookiesFile` | Created job |
| `cancel` | `id` | null |
| `retry` | `id` | null; creates a new job using current cookie defaults |
| `remove` | `id` | null; forgets one entry, keeps downloaded files; rejects active jobs |
| `pause` / `resume` | none | null |
| `clear` | `confirmed: true` | null; retains queued/running jobs |
| `settings` | empty to read, or changed settings | Settings object |
| `info` | `url`, optional `cookiesFromBrowser`, `cookiesFile` | Full yt-dlp media JSON |
| `subscribe` | none | null, then asynchronous events |
| `show` | none | null; asks a subscribed window to activate |
| `shutdown` | none | null; fails if a download is active |

Mutation requests accept a top-level `"dryRun": true`. A dry-run add returns validated options and the yt-dlp argument list. Clearing a dry-run does not require confirmation. Settings names are `mode`, `quality`, `videoFormat`, `audioFormat`, `dest`, `binary`, `maxHistory`, `cookiesFromBrowser`, and `cookiesFile`.

`videoFormat` accepts `auto` (default), `mp4`, `mkv`, or `webm`. Explicit video formats use ffmpeg conversion when needed and are ignored in audio mode. Each job stores its format choice; retry preserves it. Older settings and jobs without this field use `auto`. Removing an entry emits an updated snapshot and persists the remaining queue. Active jobs stay protected while cancellation finishes.

Cookie fields default to empty strings (no authentication). `cookiesFromBrowser` accepts yt-dlp’s browser/profile/keyring/container syntax, such as `brave` or `firefox:Profile 1::Work`. `cookiesFile` is a Netscape cookie-file path. Choose one source. On add/info requests, an explicit cookie field overrides the default source; specifying an empty source disables cookies for that request. Each job saves its selected source, and retry uses the latest default source. Cookie values are not stored in the application JSON.

Subscribe with:

```json
{"id":2,"command":"subscribe"}
```

An initial snapshot arrives immediately, followed by snapshots when state changes. Responses and events can interleave; use `id` to correlate responses.

```json
{"event":"snapshot","data":{"version":1,"paused":false,"activeId":"","jobs":[],"settings":{},"theme":{},"themeError":"","pid":123}}
```

Errors affecting the service use `{"event":"error","error":"..."}`. Window activation uses `{"event":"show"}`. Queue notifications are coalesced to at most roughly eight per second. Slow clients with over 8 MiB of queued output disconnect; reconnect and resubscribe to get the latest state. Input lines are limited to 1 MiB. Metadata requests time out after 60 seconds and at most three run concurrently.

Job fields include `id`, `url`, `mode`, `quality`, `videoFormat`, `audioFormat`, `dest`, `binary`, `status`, `phase`, `title`, `percent`, `bytes`, `totalBytes`, `speed`, `eta`, `metadata`, `file`, `log`, `error`, `added`, `started`, and `finished`. Fields appear as information becomes available; unknown percentages, speed and ETA can be null. Speed is bytes/second, ETA and duration are seconds, and timestamps are UTC ISO 8601. Status values are `queued`, `running`, `done`, `failed`, and `cancelled`; phase also describes `preparing`, `downloading`, `processing`, and recovery.
