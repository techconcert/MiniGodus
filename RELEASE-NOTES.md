What is new in MiniGodus r1:

- First release: the foundation. MiniGodus is a self-playing god game for the GeekMagic 240x240 ESP8266 clock (see docs/DESIGN.md). This release carries over the whole recovery layer, update page, rescue hotspot, WiFi, weather, time zone and sleep schedule, and flash saving from GeekQuest, renamed.
- The game itself is a placeholder scene: a grass and water field with a little shaman who wanders and goes to bed, a top bar with the time, the day, the weather icon and the temperature, and a bottom band that cycles the status lines. The real world arrives in the next releases.
- The hotspot is MINIGODUS-XXXX (password MINIGODUS). The update page checks github.com/techconcert/MiniGodus for new versions.
- MiniGodus saves in the same flash area as GeekQuest but with its own magic number, so switching between the two firmwares never loads the other's game (the WiFi and clock settings are shared).
