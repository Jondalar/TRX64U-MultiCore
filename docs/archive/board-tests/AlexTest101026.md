# Board-Tests mit Alex, 10.10.2026 (die eine M1-Liste)

Stand 2026-10-10 abends: **M1 komplett abgehakt.** RC1 (Tag `m1-rc1`) laeuft auf dem Board.
Claude fuehrt per REST/Skript; du drueckst nur, was hier steht.

## 1. Update ueber die .cfw (T49)

`trx64_update.cfw` liegt schon auf dem Stick (`/USB1`). Die EF1 darf stecken.

- [x] C64U einschalten (Flash = alter Stand t63).
- [x] Datei-Browser -> `USB1` -> `trx64_update.cfw` -> **Run Update** -> **YES**.
- [x] Anzeige "Backing up", danach "Update OK"; die Maschine schaltet aus.
- [x] Meldung "No restore bundle: the flash holds no .bit bitstream" ist hier richtig: der alte Stand kam per openFPGALoader in den Flash (roher Bitstream ohne .bit-Kopf), daraus baute dieser Updater kein Bundle, es entstand nur `TRXBKnnn.BIN`. Seit t49-raw-bundle (2026-10-10) baut der Updater auch daraus ein Bundle.
- [x] Einschalten: neuer Stand laeuft (App 98b4134, Backup `TRXBK001.BIN`).

## 2. T62: drei Sachen zum Sehen und Druecken

- [x] **Safe Mode:** aus, dann mit **gedrueckter RESTORE-Taste** einschalten und halten, bis das Menue da ist. Erwartet: Einstellungen auf Standard, im Datei-Browser kein `/flash`. Danach aus und ohne Taste ein: alles wie vorher.
- [x] **Scanlines:** Menue -> Video Configuration -> "HDMI Scan lines" auf **Enabled**: jede vierte HDMI-Zeile deutlich dunkler, Rand und Menue unveraendert. Wieder auf **Disabled** stellen, **nicht speichern**.
- [x] **Link-LED:** gelbe LED an der Netzwerkbuchse leuchtet. Kabel ziehen: nach ca. 3 s aus. Stecken: wieder an.

## 3. T16 + T57: echte Cartridges (Claude fuehrt per Skript)

Pro Cart: C64U **aus**, Cart stecken, **ein**, Claude Bescheid sagen. Das Skript fragt dann nummeriert auf Deutsch.

- [x] **EF1 (Timo's Castle)**, ca. 30 min, nur was gestern noch fehlte (Dump, Writer, Preference, Highscore sind seit 09.10. bestanden): **kaputtes CRT oeffnen -> nur ein Popup, kein Writer-Bildschirm** (T57); einmal aus, 5 s, ein; Dauerlauf, Pausen, Tastatur-Matrix: Bild j/n.
- [x] **MegaByter (Lykia)**: Port-Dump bestanden (= lykianew.crt). Die App hielt ihn fuer EasyFlash -> in der App behoben (Pruefung $DF00/$DF01), am Board bestaetigt: "MegaByter, 128 Banks", App-Dump = Port-Dump. Rest (Writer usw.) mit der neuen App: noch offen, ca. 50 min.
- [x] **GMod2 (Sam's Journey)**, ca. 75 min: wie EF1; im Spiel speichern (EEPROM); einmal aus/ein.
- [x] **EF3** (T51 §7, Schritte 1-2): kompletter Dump viermal gleich (Claude per REST, Staging ueber /Temp wegen USB-Abbruch durch Port-Last); Slot 3 geschrieben + gedumpt = gleich (T57 B11).
- [x] **T57 B3:** (REST-Start gestrichen, Alex) trex_crt einmal aus dem Freeze-Menue starten (Claude startet es zusaetzlich ueber REST).

## 4. T49: Backup zurueckspielen (BESTANDEN)

Ablauf geaendert, weil fuer die T74-Messung Gideons Stand (trex_main-90f301ce) im Flash lag: das Update von dort auf unseren Stand (ca3be3dc) legte `TRXBK_20261010_000.CFW` an, das Bundle enthaelt also **Gideons** Stand.

- [x] Einstellung geaendert (Color Scheme C128 Style), gespeichert.
- [x] `TRXBK_20261010_000.CFW` -> Run Update -> YES, Einstellungen **NO**: Restore OK, Gideons Stand laeuft, C128 Style blieb.
- [x] Zurueck auf unseren Stand per `trx64_update_ca3be3dc_20261010.cfw`.
- [x] Einstellung wieder geaendert, gespeichert.
- [x] Bundle -> YES, Einstellungen **YES**: Restore OK with settings, Color Scheme wieder Ultimate Black (wie im Backup).
- [x] Wieder unser Stand per ca3be3dc-.cfw.

## 5. RC installieren (T49, letzter Schritt) - BESTANDEN (App 3c8e4aa laeuft)

Das RC-Paket kommt aus dem CI-Lauf nach 659 (mit dem neuen TRX64-Splash, Alex); Lauf 659 (c1af874) hatte schon alles andere von heute (T74 Open Bus, Updater-Titel, GMod2-Statuszeile, Dumper mit Zwischenspeicher ueber /Temp, EF3-Slot-Dump als EF1-Datei, EAPI-Patch raus).

- [x] Datei-Browser -> `USB1` -> `trx64_update_692f643d_20261010.cfw` (liegt auf USB1, daneben das Release-Zip `trx64-integ-m1-692f643d.zip`; Tag `m1-rc1`) -> Run Update -> **YES** (Titel jetzt "TRX64 Ultimate Updater", mittig).
- [x] "Update OK", aus, ein: Splash, Menue. neuer TRX64-Splash; Claude prueft per REST die App. Dann ist das der Stand fuer die Tester.

## 6. R21 (T16): nach M2 (Alex), T76. Der EF3-Reset-Knopf funktioniert auf unserem Core.

Notizen:
- 2026-10-10 nachmittags: Ablauf geaendert. Das Bundle `TRXBK_20261010_000.CFW` entstand beim Update von Gideons Stand (trex_main-90f301ce, fuer die T74-Messung) auf ca3be3dc; es enthaelt also Gideons Stand. Restore ohne Einstellungen: BESTANDEN (Restore OK, Gideons Stand 90f301c laeuft, geaenderte Einstellung "Color Scheme C128 Style" blieb).
- Restore mit Einstellungen: BESTANDEN (Restore OK with settings, Color Scheme wieder Ultimate Black wie im Backup). Offen nur: RC-.cfw zum Schluss.
- T74 (Port-Open-Bus) mit MegaByter bestanden, Gideons Core gemessen; Rest als T75 (M2).
