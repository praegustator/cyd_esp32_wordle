# CYD Wordle, Fibble, and Don't Wordle

A self-contained, touch-driven Wordle, Fibble, and Don't Wordle game for the common ESP32-2432S028R Cheap Yellow Display: ESP32, 320x240 ILI9341 TFT, and XPT2046 resistive touch controller.

## Build

Install PlatformIO, open this folder, and run:

```sh
pio run
```

The project targets `esp32dev` with Arduino and TFT_eSPI. It uses no Wi-Fi or external runtime assets. To upload later, connect the CYD and run `pio run -t upload` (upload is not part of the normal build).

## Bluetooth keyboard

The original ESP32 on the CYD can connect to a Bluetooth Low Energy (BLE) HID keyboard. Put the keyboard in pairing mode after the game starts; the CYD scans automatically, bonds with the first BLE keyboard it finds, and reconnects to the bonded keyboard later. Classic Bluetooth-only keyboards are not supported.

Physical A-Z keys enter letters, Enter and keypad Enter submit, and Backspace or Delete erase. Key releases, modifiers, and other keys are ignored. Select a game mode with the touchscreen before using the physical keyboard. Bluetooth support and the advertised client name can be changed with `BLUETOOTH_KEYBOARD_ENABLED` and `BLUETOOTH_DEVICE_NAME` in `include/config.h`.

## Touch diagnostics and calibration

The common CYD uses a separate XPT2046 SPI bus on CLK 25, MISO 39, MOSI 32, CS 33, and IRQ 36. These pins and the raw calibration limits are in `include/config.h`. With `TOUCH_DIAGNOSTICS` and `SERIAL_DEBUG` enabled, the serial monitor prints raw coordinates, pressure, IRQ state, and mapped screen coordinates at 115200 baud.

Open the monitor with `pio device monitor`. With no touch, pressure should be near zero and IRQ should be `1`. While pressing, pressure should exceed `TOUCH_PRESSURE_MIN`, IRQ should become `0`, and raw X/Y should change. Note the raw values near the left, right, top, and bottom edges, then update `TOUCH_RAW_X_MIN`, `TOUCH_RAW_X_MAX`, `TOUCH_RAW_Y_MIN`, and `TOUCH_RAW_Y_MAX`. If an axis is mirrored, swap that axis's MIN and MAX values. After calibration, set `TOUCH_DIAGNOSTICS` to `false` to stop periodic logging.

Each press is latched until the controller reports a continuous release for `TOUCH_RELEASE_STABLE_MS`. Increase this value in `include/config.h` if a worn or noisy panel still produces double taps; decrease it if rapid intentional typing feels unresponsive.

## Controls

Tap letters on the on-screen keyboard, `ENTER` to submit exactly five letters, and `DEL` to erase. Scoring follows Wordle rules, including duplicate-letter consumption, and uses a boosted-contrast take on the classic palette: deep green marks a correct letter in the correct spot, bright yellow marks a letter present elsewhere, and dark gray marks an absent letter. The strong lightness difference between the green and yellow keeps them distinguishable for colour-blind players. After a win or the final guess, use the on-screen end-game controls.

Choose Wordle, Fibble, or Don't Wordle when the device starts. The `INFO` button at the top of the chooser opens a credits page with the license and attribution details; tap anywhere to return. The `HARD` toggle enables Wordle hard mode: revealed green letters must stay in place and yellow letters must be reused in every later guess; the setting persists in flash. The `DAILY #N` strip on the Wordle card plays a deterministic shared puzzle: every device on the same puzzle number and language gets the same answer, and finishing a daily advances the counter to the next one. Fibble begins with a random given guess, gives nine total guesses in two columns, and changes exactly one feedback tile in every row to a lie — except the winning row, which is always truthful. Tap any submitted Fibble row to open a zoomed inspector with large, finger-sized tiles; tap a large tile to mark a suspected lie (white-outlined black corner flag) or a confirmed true clue (white tile outline). Tap another submitted row to switch the inspector to it directly, tap `CLEAR` to remove all marks from the current row, and tap `DONE` only when finished marking. When a tile is confirmed true, the matching keyboard key also gets a white outline; combined with the key colour this shows what is known for sure (green/yellow: the letter is in the word, gray: it is definitely absent). The outline disappears when the mark is removed. If that row already has a suspected lie, the first tap on another unmarked tile confirms it as true; tap it again to flag it instead. When the game ends, all actual lie positions are revealed. `NEW GAME` repeats the current mode and `MODES` returns to the chooser.

The `MENU` button in the top-left corner pauses the current game and returns to the mode chooser. Each mode keeps one paused game: a card marked `IN PROGRESS` resumes that game when tapped, with the timer continuing where it left off, while its `NEW GAME` strip at the bottom of the card discards the paused game and starts a fresh one. The chooser also shows lifetime statistics per mode (games played, wins, average winning time, and current and best win streaks), which persist in flash across power cycles and are tracked separately for each language. The end-of-game panel additionally shows the guess distribution — how many wins used each number of guesses — with the just-finished game highlighted. The `RESET` button next to the language toggle clears the statistics for the currently selected language after a confirming second tap.

The display backlight dims after a minute of inactivity to save power; the first touch or key press restores full brightness without triggering any game action.

Don't Wordle reverses the goal: survive six legal guesses without entering the hidden answer. Every later guess must preserve greens, move yellows to another position, and obey eliminated-letter and duplicate-count limits. The left dashboard shows how many embedded words remain legal. Before the first turn, `RANDOM` submits a safe random starting word. Afterward, five `UNDO` uses can remove the latest accepted guess and restore its constraints. Guessing the answer loses immediately; completing six legal guesses wins.

The complete embedded 14,855-word uppercase guess list is derived from the MIT-licensed `tabatkins/wordle-list` project. It is downloaded only when generating the source, not by the device. Answers are chosen from a smaller common-word subset that is also accepted by the embedded list.

## Russian language

The `EN`/`RU` button in the top-left corner of the mode chooser switches the game language; the choice persists in flash. Russian mode uses a ЙЦУКЕН on-screen keyboard with all 32 letters (Ё is folded into Е), embedded Cyrillic glyphs from the SIL OFL-licensed Terminus font in two sizes that match the built-in Latin fonts (6x12 on keys, 8x16 in tiles), a 25,653-word five-letter guess list derived from the `mediahope/Wordle-Russian-Dictionary` project, and a curated subset of 120 common nouns as answers. Physical BLE keyboards use the standard ЙЦУКЕН mapping of QWERTY key positions, including the punctuation keys for Х, Ъ, Ж, Э, Б, and Ю. Paused games remember their language and restore it when resumed.

## License

This project is licensed under the [MIT License](LICENSE). Bundled third-party content: the embedded Cyrillic glyphs are derived from the Terminus Font (SIL Open Font License 1.1), the English word list from `tabatkins/wordle-list` (MIT), and the Russian word list from `mediahope/Wordle-Russian-Dictionary`. See the [LICENSE](LICENSE) file for details.
