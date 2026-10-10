# Native Android apps on a Linux phone: WhatsApp through Android Translation Layer

Notes and results from getting the **official WhatsApp APK** to register, restore
a chat backup, receive notifications in the background, play voice notes and
link devices by QR on a Motorola Moto G06 running postmarketOS + sxmo, **without
Android running underneath** (no Waydroid, no container, no emulator).

Everything here was found by doing it on the device. Code lives in two forks:

| Repo | Branch | What |
|---|---|---|
| [NikuzoKO/android_translation_layer](https://github.com/NikuzoKO/android_translation_layer) | `lagos` | 51 commits on top of upstream ATL: framework API fixes found by running WhatsApp |
| [NikuzoKO/bionic_translation](https://github.com/NikuzoKO/bionic_translation) | `lagos` | 2 commits: libc flag-translation fixes |
| this repo, `tools/atl/` | – | API-gap finder and stub generator (below) |
| this repo, `extras/atl/` | – | launchers, background-mode glue, camera/screenshot helpers |

Everything in the forks is intended to go upstream; they are public so the
patches and the notes are not lost.

## The landscape: ways to run Android apps on Linux

| Approach | How | Cost |
|---|---|---|
| **Anbox / Waydroid** | Run a whole Android system in an LXC container next to the host, share the kernel (binder, ashmem, GPU) | Second OS in RAM, long boot, needs kernel features. Anbox is effectively unmaintained; Waydroid is the active one. |
| **Emulators** (QEMU/Android Studio) | Virtualise a device | Heaviest |
| **Android Translation Layer (ATL)** | Re-implement the Android *Java framework API* on GTK4 + Linux libraries and run the app's dex on the real ART/libcore from AOSP. Native `.so` libs are loaded through `bionic_translation`, a shim linker that maps bionic libc calls to the host libc (musl/glibc). | Small and fast, desktop-native windows/notifications. Only as complete as the re-implemented API. |

Sources: [NLnet project page](https://nlnet.nl/project/ATL/),
[Hackaday write-up](https://hackaday.com/blog/page/418/?cat=KE),
[Waydroid on Linux phones (Purism forum)](https://forums.puri.sm/t/waydroid-wayland-android-on-linux-phones/14277),
[Waydroid vs Anbox](https://www.xda-developers.com/waydroid-android-apps-on-linux/).
The public discussion of WhatsApp under ATL stopped at "starts and shows a
screen". The notes below go further, to a working daily messenger.

### Why not Waydroid here

It was tried first on this phone and removed:

* The stock GKI kernel has `CONFIG_PID_NS` / `IPC_NS` **off**. `waydroid container stop` then kills only `lxc-start`; the rest of Android
  (surfaceflinger, zygote…) is reparented to PID 1 and keeps running. Ten leaked instances meant load average 50 and 1.8 GB in zram.
* Without a PID namespace Android shares the host PID space (`pid_max` 4194304). **32-bit bionic mutexes store only a 16-bit owner tid**, so any 32-bit Android process
  with a PID above 65535 deadlocks on its own mutex (`boringssl_self_test32` hung forever in `FUTEX_WAIT`). `kernel.pid_max = 32768` fixes it.
* It is a second phone living inside the first one. ATL has none of these problems.

## State

On the Moto G06 (MT6768, 4 GB, software GL):

| Feature | State |
|---|---|
| Register a number by SMS | ✅ |
| **Restore a chat backup during registration** | ✅ (needs a non-passkey backup, see below) |
| Text, images, voice notes (send/receive/play) | ✅ |
| Background operation: notifications with the window closed, wake-from-suspend on the app's keep-alive alarm | ✅ |
| Notification sound + vibration through dunst | ✅ |
| Link a device by QR (rear camera) | ✅ |
| Keyboard, non-ASCII input, multi-line composer | ✅ |
| Dark theme following the system | ✅ |
| Calls | not tested |
| Pixel-exact look | partial: rounded corners on some FABs/cards still square |

## Method that worked

1. **Measure the gap, don't guess.** `tools/atl/atl_apicheck.py app.apk` parses the app's dex, ATL's `api-impl.jar` and the ART core jars and lists every `android.*` class/member the app references that does not resolve (following superclasses and interfaces through the app's own classes, ~22 s for WhatsApp).
   WhatsApp started at **1792 missing members**.
2. **Auto-stub the long tail.** `tools/atl/atl_autostub.py` writes one-line `// ATL-autostub` no-op stubs into `src/api-impl` (static-ness from `invoke-static`/`sget` scans, builders return `this`), and `atl_stubloop.sh` javac-compiles and prunes lines that don't compile. 1792 → **252** missing members. Hand fix-ups needed for stubs that override protected methods, `throws` clauses and lost implicit constructors.
3. **Run the app and read *the app's own log*.** WhatsApp writes `files/Logs/whatsapp.log` inside its data dir. It is far more useful than ATL's stderr: it logs state machines, network results and even `UXLogging/clicked:` for taps, which maps a crash to the user action that caused it.
4. **Replace stubs with real behaviour** only where the app depends on the result (below).

## What had to be real (and what each bug looked like)

### bionic_translation / native layer

| Symptom | Cause | Fix |
|---|---|---|
| `libglog.so` fails to load | missing `__strncat_chk`, `arc4random(_buf)`, `getprogname`, `mallinfo`, `funopen`, `__cxa_thread_atexit_impl` | an `LD_PRELOAD` stub: the shim linker resolves `bionic_<name>` via `dlsym(RTLD_DEFAULT)` first, then `<name>`, so a preloaded library can fill gaps |
| SIGSEGV right after entering the 2FA PIN | `readdir` copied a fixed 256-byte `d_name` past musl's smaller dirent | copy only the name |
| "Connecting…" forever | WhatsApp's DNS code turns IPs into PTR names via `getnameinfo`, which had no wrapper, and **bionic's `NI_*` and `AI_*` values differ from musl's** (bionic `NI_NUMERICHOST`=0x2 is musl `NI_NUMERICSERV`; bionic `AI_NUMERICSERV`=0x8 is musl `AI_V4MAPPED`) | translate the flags in `getaddrinfo`, add a `getnameinfo` wrapper |

### Activity/Application lifecycle (ATL api-impl)

* `startActivityForResult` ran the target **inline**, so the callee started, then the caller was put on top: the callee was paused, never seen and never finished, and `onActivityResult` never arrived. WhatsApp's "look for a local backup" step lives in exactly that callback, so **a restore was never offered**. Posting the start fixed it. Results are now delivered once, on finish (default `RESULT_CANCELED`) and again on destroy.
* `Dialog.show()` called `onCreate` every time; androidx aborted with "SavedStateRegistry was already restored" on the second dialog.
* `Application.registerActivityLifecycleCallbacks` was a no-op, so the app believed it was always in the background; its chat connection was closed with `reason=8` every ~47 s and the "Finishing setup" splash looped.
* `AlarmManager` ignored `cancel()` and re-`set()`; every reset logout timer still fired. Alarms are now keyed like Android's (`PendingIntent` type/requestCode/filterEquals, or listener).
* `requestPermissions` only logged. Now answers immediately from `checkSelfPermission`. The contacts permissions are granted with `ATL_UGLY_ENABLE_CONTACTS`, otherwise the restore screen re-asks forever.

### Registration quirks worth knowing

* **A crash between the SMS check and writing `files/me` leaves a half-registered account** (prefs say verified, no identity file): every start lands on the name screen with `me-not-loaded`. The only way out is a fresh data dir and one uninterrupted registration.
* Until `files/me` exists, restarting the app is dangerous, so snapshot the data dir at each registration stage (this also lets you replay a step without a new SMS).
* **Backup restore:** a passkey/E2E-encrypted backup needs Credential Manager / Google Play Services and cannot work here (the key request times out after ~32 s and the UI silently returns to the restore screen). Switch the source phone to a non-E2E backup (`msgstore.db.crypt14`); the server hands the key out during registration. Put the file at `<data>/WhatsApp/Databases/` *before* registering.
* After a restore, "Finishing setup" is a full-text-index migration (~1 M messages ≈ 10 min on this phone); one app restart clears the banner.
* IPv6 on some home routers hands out unusable global addresses; WhatsApp resolves `AAAA` first and hangs 60 s per request. Disable IPv6 on that connection.
* `ATL` reports `SDK_INT = the APK's minSdk`, and WhatsApp defaults to forced-light on SDK < 29. AppCompat applied that through `ContextThemeWrapper.applyOverrideConfiguration`, an autostub, so half the UI was light, half dark. Implementing it and making every `Resources` instance follow one night-mode choice fixed it.

### Platform services

| Area | What was added |
|---|---|
| Network | real `ConnectivityManager` callbacks (immediate `onAvailable`/`onCapabilitiesChanged`), `NetworkCapabilities`, "available" when a default route exists. Without it images were skipped by auto-download and manual downloads kept only the first progressive-JPEG scans. |
| Audio | `MediaCodec` audio output buffers 8 KiB → 128 KiB and `swr_convert` clamped to the buffer (a long opus frame overflowed the heap and voice notes were silent); `AudioManager` stream volume/mute from ALSA Master (a constant 0 made the app warn "volume is off"); call routing state |
| Media provider | `MediaStore` served from the XDG media dirs instead of a file-chooser popup per query |
| Contacts | read-only `com.android.contacts` provider backed by a `number<TAB>name` file |
| Notifications | skip empty ones; ongoing ones handled; `Icon` real minimal implementation (WhatsApp's voice-note playback notification crashed on `Icon.loadDrawable` returning null) |
| Camera | Camera1 API fed by an external frame source (`$ATL_CAMERA_CMD`), NV21 preview callbacks, `SurfaceHolder` drawing; QR linking works |
| Text input | `onTextChanged` count in UTF-16 units (UTF-8 byte counts made `ñ`/`á` index past the end → `IndexOutOfBoundsException` → SIGABRT); multi-line `EditText`; `wrapper_widget_set_jobject` was never called for `EditText`, so subclasses never got `onDraw` and WhatsApp's self-drawn "Message" hint never appeared |
| Layout | `ScrollView.fillViewport`; remeasure children that GTK stretches to the viewport; text sizes in device pixels (points rendered 4/3 too large at 96 dpi) |
| Drawing | tint/color filters, `Canvas.clipPath`, `Paint` hairline default, outline providers → CSS `border-radius` + clip, `View` drawable state (`pressed`/`selected`/…) |
| Input method | drop the IM context's client widget when that view is destroyed (GTK's Wayland IM used the freed widget) |

### Running it as a phone app (no GMS)

WhatsApp keeps its own persistent connection, so it needs **no Google Play Services**; FCM token fetch fails harmlessly. To get notifications with the screen off:

* `ATL_BACKGROUND=1`: closing the last activity hides the window and keeps the app running (`onPause`/`onStop`); SIGUSR1, a D-Bus activate or a notification click shows it again. `ATL_START_HIDDEN=1` starts it that way.
* `AlarmManager` runs on `elapsedRealtime` (BOOTTIME) with a dispatcher thread, because `Handler.postDelayed` is monotonic and stops in suspend. Wake-up alarms take a 5 s kernel wakelock and publish the earliest due time so the suspend script can `rtcwake` for it. WhatsApp's keep-alive is a ~4.5 min periodic alarm.
* `PowerManager.WakeLock` → `/sys/power/wake_lock`, always with a timeout re-armed every 30 s so a dead app frees its locks within a minute. Writing needs `CAP_BLOCK_SUSPEND`, granted to a tiny launcher through file capabilities.
  **Gotcha:** the ambient capability leaked into glycin's `bwrap`, which then refused to start, so *every* image decode returned null.
* Notification sound/vibration: dunst 1.13 reads `dunstrc.d/*.conf` only next to an existing `dunstrc`.

## Gotchas that cost hours

* **ATL's `src/api-impl/meson.build` lists every `.java` file explicitly.** A new file that is not added there is silently not built; the "fix" is simply absent from the jar.
* ATL's dex has **no `invokedynamic`**: no lambdas in `api-impl`; use anonymous classes.
* `sys_config.uiMode` is shared by all `Resources` through one native `AssetManager`; every new `Resources` pushing `uiMode 0` flips the whole app to "not night".
* `GSK_RENDERER=cairo` fixes magenta boxes instead of text on software GL (glyph atlas lost).
* Screenshots: under sway's gles2 renderer `grim` returns all zeros. `extras/atl/atl-shot` asks ATL to snapshot *its own* window (a request file + `SIGUSR1`), which works hidden or with the display off.
* Keep a hidden scratchpad window in mind: its size is the pre-show size, so screenshots look zoomed.
* A `ProgressDialog` left over from a failed request never dismisses: restart the app.
* On musl, a library path lookup can find an old `libandroid.so.0` in `/lib` before your `/usr/local` build: set `LD_LIBRARY_PATH` explicitly.
* The input-method path binds `zwp_text_input_v3` lazily on the app's first IM context; if that context never gets a client widget the on-screen keyboard never opens in chats. Traced with `WAYLAND_DEBUG=1` on both ATL and the keyboard.

## Reproduce

Build ATL from the `lagos` branch (Alpine `edge`, plus `libsecret-dev` and `gtk4-layer-shell-dev`) and `bionic_translation` from its `lagos` branch (`g++ libunwind-dev libbsd-dev elfutils-dev`), install under `/usr/local`, then see `extras/atl/` for the launchers (`atl-run`, `atl-whatsapp`, `atl-shot`, wake-lock helper). Run `tools/atl/atl_apicheck.py` on any other APK to see how far it is from working.

## Credits

Work by [NikuzoKO](https://github.com/NikuzoKO), developed together with
**[Claude](https://claude.com/claude-code) (Anthropic) via Claude Code**, used as
the AI engineering tool for investigation, debugging, patches and these notes.
Built on [Android Translation Layer](https://gitlab.com/android_translation_layer/android_translation_layer)
and [bionic_translation](https://gitlab.com/android_translation_layer/bionic_translation)
by their upstream authors, postmarketOS, sxmo, and Motorola's published kernel/connectivity sources.

WhatsApp is a trademark of Meta. This repository contains **no WhatsApp code or
assets**, only changes to ATL and notes on interoperability.
