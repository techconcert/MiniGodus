# Recovery guide (for the owner of the clock)

You do not need to know how to program to use this page. It tells you what to do when something goes wrong, what to check before you install a new version, and what to look at after you install one.

Two addresses matter:

- Your clock on the home WiFi: `http://<IP address>/`. The IP address is shown on the bottom band of the clock screen (the band cycles every few seconds between the IP address, the rescue network name and password, the firmware version and the world day) and is in your router's list. (`minigodus.local` is not answered: it needs 19 KB of firmware.)
- The clock's own rescue network: join the WiFi called `MINIGODUS-XXXX` (the password is `MINIGODUS`), then open `http://192.168.4.1/`. This network is switched on for the first 10 minutes after every power-on, and whenever the clock cannot find your home WiFi.

The page `/update` (for example `http://192.168.4.1/update`) installs a new version of the firmware. It is the most important page: every version of MiniGodus has it, and the clock starts it before it starts the game, so a game problem cannot hide it.

## What can go wrong, and what to do

| What you see | What to do |
|---|---|
| The clock works, but something in the game looks wrong | Open the update page and install the last version you trusted (an older file from the `builds` branch, folder `firmware/obsolete/`, or the release you trusted). |
| The screen shows SAFE MODE and the name of the rescue network | The game crashed several times in a row, so the clock stopped running it. The update page still works. Install a fixed version, or press **Restart** on the status page to try the game again. |
| The game crashes every few minutes | After 5 crashes the clock goes into safe mode by itself. Then install a fixed or older version. |
| The game state looks like nonsense | Press **Restart** on the status page. If the saved world is the problem, restart in safe mode and install a fixed version; after 2 crashes the clock stops loading the saved world by itself. |
| The screen is black or garbled | The web page does not depend on the screen. Open it over WiFi or through the rescue network. |
| The clock is no longer on your home WiFi | Unplug it, plug it in, and within 10 minutes join `MINIGODUS-XXXX` and open `http://192.168.4.1/wifi` to enter the WiFi name and password again. |
| The clock lost the WiFi because the router was off | Nothing to do. The clock keeps trying with longer and longer pauses and connects again by itself. The bottom band shows the rescue network name and password meanwhile. |
| The time shows `--:--` | The clock has not reached a time server yet (or the power was cut). It fixes itself once the WiFi is back. |
| You want the original clock firmware back | On the update page choose the original `SDPro_V1.0.6...bin` file. |
| A file you chose on the update page is refused | Read the message. Nothing was installed and the clock keeps running. (The strict upload checks are off; the page warns instead, see Switches.) |
| No rescue network after unplugging and replugging, and no web page | This is the one case that cannot be fixed without a cable and a serial adapter. Everything in this guide exists to keep it from happening. |

Things that are **safe** to do at any time: opening any page, **Restart**, **Restart in safe mode**.

## Your world and settings: what survives what

MiniGodus saves its settings and the world to flash: only sectors 0x80000-0x82FFF, by `firmware/store.cpp`, as a ring log of CRC-checked 320-byte records (36 slots, newest valid record wins, a torn write is ignored). It never runs in safe mode, during a firmware update, or if the image is above 0x7D000 bytes; writes are rate limited and verified; after 2 or more crashes the saved world is kept aside and not loaded. If anything looks wrong, flash the previous build or the stock firmware: both ignore the area. The status page lines "Saved to flash" and "Loaded from flash" show what happened.

| Event | Settings | World |
|---|---|---|
| Restart button, update, crash, watchdog, automatic restart | Kept. | Kept (as of the last save: every 20 minutes and at each new world day). |
| Unplugging the clock | Kept (the saved copy is loaded at boot). | Kept (as of the last save). |
| Flashing the stock firmware | Lost (not needed). | Lost (not needed). |
| Switching between **GeekQuest** and MiniGodus | Kept (the clock settings are shared). | Not shared. Each game has its own magic number in the save, so a world saved by the other firmware is ignored, never loaded, and replaced by the next save. |

The status page also shows read-only lines about two flash areas: **Stock data sector 0x3FA000** (known to hold the stock firmware's own data, never to be written) and **Free sector 0x80000** (in use by the save, with its first 16 bytes, plus where the running image ends). Sector 0xFF000 is in use (a firmware update is staged there), which is why the save area is 0x80000, above the running image and below any staged update. Nothing else is ever written or erased by the clock itself; the only other flash writing is the firmware update through the update page.

## Switches

- **Automatic restarts** (on): when WiFi has been gone for hours and the ladder of reconnect attempts did not help, when memory stays very low for a minute, and once a week at night (04:00 to 04:30 when the clock knows the time, otherwise while the shaman sleeps) if nobody has used the web page for 5 minutes and nobody is on the rescue network. They are not allowed in the first 30 minutes after power-on, never during a firmware upload (a pending automatic restart waits until the upload is over), and every restart counts in the crash counters: if the clock keeps restarting it ends in safe mode. The weekly restart can be switched off on the status page.
- **Strict upload checks** (refuse a file that arrived damaged or is not a MiniGodus build). Still off: the update page shows the file's CRC32 number and warns, and the clock records what the check would have said, but it installs exactly as before. Needs your written OK.
- **WiFi modem sleep off** (`GQ_NET_NO_SLEEP`, about 0.25 W more power). Approved 2026-11-11 and ON (the backend page was dropping while the hotspot stayed up).
- **Flash saving** is limited to sectors 0x80000-0x82FFF (see above); the build checks and the source lint refuse any other flash write.

The update page has a browser-side "Check for a new version" button to the update page. It runs only in your browser (GitHub is read from there), the clock sends nothing to GitHub, and the downloaded file goes through exactly the same upload route and checks as a file you pick by hand. There is no serial debug output (Serial is not used anywhere).

## Before you install a new version (pre-flash checklist)

1. The file comes from the `builds` branch, folder `firmware/` (not `obsolete/`), its name starts with `SDP_MiniGodus_`, and the GitHub check for that version is green.
2. You have the last known-good `.bin` and the original `SDPro_V1.0.6` file on the phone or computer you are installing from.
3. The clock has steady power and will not be moved for two minutes.
4. On the clock's status page: **Room for update** is larger than the new file, and the crash counter is 0.
5. Nothing to back up: settings and the world are saved in the clock and survive the update.
6. Choose the file on the update page, check the CRC32 and the message under it, press **Upload and install**. Wait for **Installed N bytes**. Do not unplug for 60 seconds.
7. If the page does not come back within 2 minutes: unplug the clock and plug it in once, join `MINIGODUS-XXXX` within 10 minutes, open `http://192.168.4.1/`.

## After every install (hardware smoke checklist)

For every version:

1. The status page opens over your home WiFi and shows the new firmware version.
2. For the first 10 minutes `MINIGODUS-XXXX` is visible. Join it and open `http://192.168.4.1/update`: the update page appears (do not upload anything).
3. `/update` also opens over the home WiFi.
4. **Restart in safe mode** shows SAFE MODE with the rescue network name and the status page works. **Restart** brings the game back.
5. Note from the status page: free heap, FPS, longest loop, last restart reasons.

In addition, for a release that changes the recovery layer:

6. Change the time zone, brightness and colour depth, then press **Restart**. They are still set and the world day is unchanged.
7. Unplug and plug in: settings and the world are back as of the last save.
8. The status page has the **Network** and **Health** boxes. Open `/info`: one line of JSON you can paste into a bug report.
9. Choose a random file (not firmware) on the update page: you get a warning and the clock keeps running. (With the strict checks off the upload would still be accepted if you pressed the button, so do not.)
10. Choose the original `SDPro` file but **do not press upload**. Write down the CRC32 the page shows and send it to the maintainer (it lets the clock recognise the original firmware exactly).
11. Look at the **Free sector 0x80000** line on the status page: it shows the save area and where the image ends. The image must end below 0x7D000.
12. Watch the bottom band for a minute: it alternates between the IP address, the rescue network, the firmware version and the world day.

For a game release in addition: the shaman is on screen or asleep in his bed at all times, and after a week the crash counter has not gone up.

## What the checks in the build protect

Every `.bin` is refused by the build (no file is produced) unless it has the update page, the rescue network, safe mode and WiFi routes, fits in 500 KB, uses the original flash layout, contains no code that could touch the original firmware's files or settings, and uses about the same amount of memory as before. See `tools/check_bin.py`.

### What the checks require, in detail

- `check_bin.py` requires every recovery string: the update page marker (exactly once), `/update_ota`, `/update`, the Updater's "Not Enough Space", the `MINIGODUS-%02X%02X` rescue network name, `GQ-RECOVERY-REV`, `/safemode`, `/wifi`, `/config`, and the read-only probe lines. It refuses the flash journal marker `GQJ1`, and requires `_EEPROM_start`, `_FS_start` and `_FS_end` at the stock addresses. The forbidden-symbol list (EEPROM, LittleFS, SPIFFS, HTTP updater, ArduinoOTA, HTTPClient, TLS) is enforced, and the quest build fails if the display-test scene code is linked in. Images must stay under 500 KB (CI also caps them at 470 KB).
- `lint_safety.py` L2: no firmware file may call `flashWrite`, `flashEraseSector`, EEPROM, LittleFS or SPIFFS functions, except `firmware/store.cpp` in the save area only (rule L14); `flashRead` is allowed in `prefs.cpp` and `store.cpp`. L8 requires `-DGQ_AUTO_RESTART=1` and `-DGQ_STRICT_UPLOAD=0`, and rejects `GQ_FLASH_SAVE` anywhere and `-DGQ_TEST_SCENE=1` outside `[env:test]`. L9 forbids floating point formatting and parsing (it would pull 14 KB of C library back in).
- `recovery_guard.py` watches the recovery-contract files; a change needs a `RECOVERY-CHANGE:` line in a commit message and a new `GQ_RECOVERY_REV` in `firmware/safety.cpp`.
- The `GQ_` prefix in code names is inherited from GeekQuest; it was deliberately not renamed so the recovery layer stays comparable with it.

## What cannot be protected

- A power cut during the few seconds after an install while the clock copies the new firmware into place (rare). Keep the power on for 60 seconds.
- A bug in the recovery code itself that neither crashes nor serves the page. That code is small, rarely changed, and every change to it is marked in the commit and in the build.
- Hardware faults.
- Someone else on your WiFi or near the clock opening the update page (it has no password on purpose: a password could lock you out).
