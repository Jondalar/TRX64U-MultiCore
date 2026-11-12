# Architektur-Vorschlag nach dem Reißbrett (2026-10-05)

Auftrag (Alex, 2026-10-04): (1) Drop-in-Ersatz für Gideons C64U-Core, (2) TRX64-Monitor,
(3) Timewarp. Edge-Kram darf fallen. Realistisch, keine Schönfärberei.

Zahlen: CI-Build main-latest (run 116, Reports vom 2026-10-04 09:18 UTC,
`c64_basic.utilization_hier.rpt`), von mir im Report nachgeprüft. **[M]** = gemessen,
**[S]** = Schätzung (nicht gebaut). Schätzungen sind Schätzungen, erst ein Build zählt.

## 1. Wo wir stehen

| | Wert | Grenze |
|---|---|---|
| LUT | 27.644 [M] | 32.600 (84,8 %) |
| **Slices belegt** | **8.147 [M]** | **8.150 (99,96 %)** |
| BRAM-Tiles | 62 [M] | 75 |
| DSP | 13 [M] | 120 |
| MMCM / GTP | 5 / 4 [M] | 5 / 4 (alle belegt) |
| WNS | +0,010 ns [M] | 0 |

**Das eigentliche Limit sind die Slices, nicht die LUTs.** Der Chip ist bei 85 % LUT praktisch
voll gepackt. Folge: jeder Merge kippt irgendwo das Timing (letzte Woche 10 rote Läufe
wegen Timing, u. a. rvlite-Multiplizierer, TMDS, pix_layer, jeder Turbo-Build). Mit
+0,010 ns Reserve ist main heute nur zufällig grün.

Beleg aus dieser Nacht: `t44-open` ändert nur eine Generic (`g_low_limit` 0x40 → 0) und
fällt mit WNS −0,317 ns durch, Pfad rvlite-Registerfile → Multiplizierer (CI run 404). Der
Pfad hat mit dem Gate nichts zu tun — das Placement würfelt nur anders.

**Realistisches Ziel:** ≤ 26.000 LUT (≈ 80 %), damit Placement Luft hat und WNS in jeder
Domain ≥ +0,3 ns bleibt. Darüber ist jeder Build ein Würfelwurf.

## 2. Was schon beschlossen, aber noch nicht drin ist (kostet Platz)

| Posten | LUT | Quelle |
|---|---:|---|
| T21 Trace-Former | +443 | [M] Build t21-p4 |
| T16 Q8 / Writer | +31 | [M] Build t16-complete |
| A06 Monitor-Stopp = Freeze-Session | +100…300 | [S] |
| G06 Cycle-Steal (I/O bei laufendem C64) | +150…300 | [S], Agent misst |
| A08.3 Cart-Banking aller Typen im Ring | +100…200 | [S] |
| T62 Kleinkram (Safe Mode, $D7FF, Eth-LED, IEC-Reset, Scanlines) | ≈ +100 | [S] |
| A05 Mixer hinter reDIP | ≈ +50, 2 DSP | [S] |
| T59 Paletten | ≈ +100 | [S] |
| T60 REU 16 MB / Pfad | ≈ +30 | [S] |
| **Summe ohne Turbo** | **≈ +1.100…1.550** | |
| T29 Turbo | +627, +8 BRAM | [M] Build t29-turbo |

Ohne Kürzung: ≈ 28.750…29.200 LUT (88–90 %) ohne Turbo, ≈ 29.400…29.800 mit Turbo.
**Das passt nicht verlässlich.** Präzedenzfall: der Integrations-Build vom 2026-10-02 ließ
sich nicht platzieren.

## 3. Was wir streichen können (gemessen)

| Kandidat | LUT [M] | Braucht ihn Ziel 1/2/3? | Folge fürs Streichen |
|---|---:|---|---|
| LED-Effekte (`led_engine` ×2 in bling_io) | 560 | nein (A04 schon entschieden) | Tastatur-Bling, SHIFT LOCK, Gehäuse-LED an/aus bleiben |
| UDP Video-/Audio-Stream (`udp_stream`, `udp_audio`, `stream_mux`) | 587 | nur Drop-in, Edge | kein Netzwerk-Video/-Audio-Stream |
| **1541-Emulation** (`drive_unit`, `drv_dbg`) | **1.869** | Drop-in ja; Monitor/Timewarp nein | siehe 3.1 |
| JTAG-Debug (`jtag_dbg`, `rv_dbg`, `con_ring`) | 761 | nein, nur Entwicklung | nur im Alex-Build weg; Dev-Build behält es (trxdbg) |
| T29 Turbo (noch nicht drin) | 627 + 8 BRAM | Drop-in ja (Gideon kann Turbo) | Timing-Risiko Nr. 1, IEC unter Turbo heute kaputt |

Nicht streichbar, weil alle drei Ziele daran hängen: C64-Kern (8.005), Gideons
ultimate_sys ohne Laufwerk (≈ 4.800: rvlite 2.045, USB 686, WiFi/ESP32 496, Ethernet 417 …),
MIG 3.400, Freeze/Session 1.084, Engine 1.251, pix_layer 741, trx_sys 621, trx_dbg 672,
HDMI/Overlay/Scaler/c64_text ≈ 1.650, Tastatur 721. `c64_text` bleibt: er zeichnet das
Freeze-Menü im C64-Bildschirm-Modus.

### 3.1 Die eine große Frage: 1541-Emulation

Ohne `drive_unit` (−1.869 LUT):
- geht weiter: PRG/D64-Inhalte per DMA starten (App lädt das PRG direkt in den Speicher),
  CRT, echte Laufwerke am DIN-Port (IEC-Bus bleibt, 9 LUT).
- geht nicht mehr: Diskette mounten und `LOAD"*",8,1`, Fastloader, Spiele mit Nachladen von
  D64/G64, Schreiben auf Disk-Images.

Für „Drop-in“ ist das ein echter Verlust. Für Monitor + Timewarp ist es egal (der Ring
stellt das Laufwerk sowieso nicht wieder her, A08.2).

## 4. Szenarien

| Szenario | LUT [S] | Urteil |
|---|---:|---|
| A: alles Beschlossene, nichts streichen, ohne Turbo | ≈ 28.900 | passt nicht verlässlich |
| B: A − LED-Effekte − UDP-Streams | ≈ 27.750 | wie heute: Würfelwurf |
| C: B − JTAG-Debug (Alex-Build) | ≈ 27.000 | knapp, Turbo passt nicht dazu |
| D: B − 1541-Emulation | ≈ 25.900 | **passt mit Reserve**; mit Turbo ≈ 26.550, knapp über dem Ziel |
| E: C − 1541-Emulation | ≈ 25.100 | passt mit Reserve; mit Turbo ≈ 25.800, passt |

**Ehrlich:** Drop-in *komplett* (inkl. 1541 und Turbo) plus Monitor plus Timewarp passt auf
den 50T nicht mit Reserve. Es geht nur, wenn die 1541-Emulation oder der Turbo fällt.

## 5. Architektur-Regeln für den Rest

1. **Ein Stopp-Mechanismus.** Breakpoint, Observer-Stopp, Step, Pause = Freeze an einer
   exakt bestimmten Stelle (`freeze_resp`-Session). Kein zweiter Debugger-Pfad. Abnahme:
   jeder Lese-Zugriff nach einem Stopp == Freeze-Lesung am selben Zyklus; Weiterlaufen
   bitgleich (Raster-Test wie T12).
2. **Monitor-Korrektheit als Test, nicht als Behauptung.** Golden-Vergleich: gleicher
   Zustand im TRX64-Emulator und auf dem Board, alle Lenses, alle Verben.
3. **Timewarp im festen Rahmen** (A08–A13): RAM, CPU, VIC-Register, CIAs, Cart-Banking; REU an
   = kein Ring; kein Dump/Load. Nichts darüber hinaus in M1.
4. **LUT-Budget als CI-Gate.** Build schlägt fehl über 26.000 LUT oder WNS < +0,3 ns in
   einer Domain. Ein Feature, das nicht passt, wird nicht gemergt — es wird entschieden,
   was dafür geht.
5. **Zwei Build-Varianten:** `alex` (ohne JTAG-Debug, falls Szenario C/E) und `dev` (mit).
6. **„Fertig“ = jeder Spec-Abschnitt mit Beleg** (Code + Board). Agenten-Ergebnisse werden
   von mir gegen Code und Entscheidungen geprüft, bevor sie zu dir kommen.

## 6. Reihenfolge

1. **Kürzen und messen:** LED-Effekte und UDP-Streams raus (+ deine Entscheidung zu 1541 /
   Turbo / JTAG). Ein Build, echte Zahlen. Budget-Gate in die CI.
2. **Monitor:** A06 (Stopp = Freeze), T21-UI (40 Spalten, Spec §8), Break öffnet Monitor,
   G06 falls er ins Budget passt. Golden-Test.
3. **Timewarp abschließen:** Cart-Banking im Ring, REU schaltet Ring ab, Dump/Load-Code raus.
4. **Drop-in-Rest:** T16 (Q8, Writer, Cart-Session), T60 REU-Pfad, T62, T59 Paletten,
   T61 Uhr, A05 Mixer, A07 (REST Cart-Zustand, Power-off-Warnung).
5. **Board-Testplan** aus dem Audit in einem Rutsch.

## 7. Entscheidungen, die ich von dir brauche (einzeln)

1. 1541-Emulation behalten oder streichen?
2. Turbo (T29) behalten oder streichen?
3. UDP-Streams streichen? (Empfehlung: ja)
4. Alex-Build ohne JTAG-Debug? (nur nötig, wenn 1541 bleibt)

## Entscheidungen (Alex, 2026-10-05)

| # | Frage | Entscheidung |
|---|---|---|
| 1 | 1541-Emulation | **bleibt** (eigentlich gehörte die 1581 auch dazu; bleibt T56, M2) |
| 3 | UDP video/audio streams | **bleiben** — viele bedienen ihr U64/C64U mit Remote-Tools darüber |
| – | Floppy in software on the RISC-V / one 6502 core time-shared by C64 and 1541 | **verworfen** — rvlite schafft keinen zyklusgenauen 1541 in Echtzeit; geteilte CPU: in C64RE wurde es erst gut, als die Floppy-CPU wirklich getrennt war (Fastloader/Floppycode-Loader sind pingelig) |
| 4 | JTAG-Debug im Release | **RC/Release ohne JTAG-Debug** (jtag_dbg, rv_dbg, con_ring: −761 LUT); Dev-Build behält ihn. Budget-Gate gilt für den Release-Build |
| 5 | Budget | **Kein hartes 26.000-Gate** (Alex: "wenn es baut, ist es ok"). Maßstab ist der Release-Build: Timing erfüllt (WNS ≥ 0 in jeder Domain). Ziel bleibt Reserve; die Optimierungs-Branches (opt-bling −473, opt-frz −144, opt-kb offen) und Release ohne JTAG (−761) gehen rein |
| 6 | DDR2-Controller | **Option B: eigener schlanker Controller statt MIG (Alex 2026-10-05: "das lohnt sich richtig")**, nach dem Vorbild von Gideons Altera-Controller; zuerst im ddr2_test-Design mit Speichertest auf dem Board, erst danach Tausch in c64_basic. Spec folgt (docs/specs/OPT-ddr2.md). Option A (opt-mig, -313) ist gemergt |
| 7 | Ring- und Standbild-Formate | **Frei nach unserer Sicht optimierbar (Alex 2026-10-06):** Ring-, Standbild- und Record-Formate müssen nicht zum TRX64-Emulator passen; nur wir und Mike nutzen beides, Mike noch kaum. Austausch mit TRX64 bleibt M2/M3 (A09). |
| 8 | pix_layer schlanker | **Ja (Alex 2026-10-06):** Standbild-Aufnahme als Schieberegister mit einem Puffer, Konfigurations-Register nicht mehr rücklesbar (nur Status), Spec docs/specs/OPT-pix.md |
| 9 | Turbo (T29) | **Wieder rein in M1 (Alex 2026-10-06):** der Platz ist da (Iteration 2: 23.709 LUT); auf dem neuen Stand neu aufsetzen und messen, IEC-Bremse unter Turbo klären. Ersetzt "Turbo raus aus dem Budget" vom 5.10. |
| 10 | Integration | **Ein Integrations-Branch `integ-m1`** mit allen fertigen Branches statt vieler Einzel-Merges (Alex 2026-10-06: "viel zu fragmentiert"); ein CI-Build, ein Board-Test, dann nach main |
| 11 | Lizenz | **Unser Code: GPL-3.0-or-later (Alex 2026-10-06).** Dags reDIP-Teile bleiben CERN-OHL-S-2.0, unser GPL-Code ist im Sinne von CERN-OHL-S "Available Components" (Dags Vorschlag). Übersicht: LICENSING.md, Text: LICENSE |

Seit 2026-10-07 steht jede dieser Entscheidungen auch als ADR in docs/adr/ (Index: docs/adr/README.md; "Formerly: decision N" in der jeweiligen Datei). Alte Verweise wie "decision 9" lösen dort auf.

## Umsetzung (Stand 2026-10-06)

Alle fertigen Branches sind in `integ-m1` (Entscheidung 10) zusammengeführt; CI-Zahlen (LUT/WNS) und Board-Test stehen aus (Board-Test 2026-10-06 abends). Die App dazu: `integ-m1-app` (Gitea TREX/1541ultimate).

| Schritt | Stand | LUT c64_basic (gemessen) | WNS |
|---|---|---:|---:|
| main 448fc36 (rvmul, CI-Cache) | gemergt | 27.660 | +0,340 |
| opt-kb (Tastatur-Scanner, formal gleichwertig) | gemergt, main 8423823 | 27.275 | +0,027 |
| hdmi-cea50 (50 Hz wie Gideon, T40 §11) | gemergt, Acer getestet | – | +0,054 (Branch) |
| opt-mig (MIG ohne XADC, 2 Bank-Maschinen, docs/specs/OPT-mig.md) | gemergt, Board-Test 2026-10-06 | 26.962 (Branch) | +0,200 |
| SLIM (docs/specs/SLIM-state-path.md, D1-D6) | gemergt (main), App slim-core-app in main | – | – |
| opt-pix (Standbild-Schieberegister, OPT-pix.md) | in integ-m1 | grüner CI-Lauf 450 (Branch) | – |
| opt-bling (LED-Effekte) | in integ-m1 | 27.171 (alter Stand) | +0,170 |
| ddr2-swap (eigener DDR2-Controller, OPT-ddr2 §9) | in integ-m1 | grüner CI-Lauf 449 (Branch) | – |
| rtc-counter (T61, nur App: rtc-sw-app) | in integ-m1 / integ-m1-app | – | – |
| t16-complete (T16 Q8, dma_bus, Writer streamt CRT; App t16-complete-app) | in integ-m1 / integ-m1-app, die Freeze-Session-Teile entfallen mit SLIM | – | – |
| g06-cycle-steal (G06 = T63) | in integ-m1 | 27.636 (Branch, Lauf 403) | +0,014 |
| t44-open (Flash-Gate offen, T44/T50 nach M3) | in integ-m1 | – | – |
| t29-turbo (T29, Entscheidung 9; App t29-app) | in integ-m1; IEC unter Turbo weiterhin kaputt, Turbo per Default aus | +627 / +8 BRAM (Lauf 354, alter Stand) | +0,082 (Lauf 354) |
| opt-frz | entfällt: SLIM D1/D3 nimmt Commit-Liste und Zähler ganz raus | – | – |
| integ-m1 gesamt | CI-Build und Board-Test 2026-10-06 abends | offen | offen |

Seite mit Iterationen: https://claude.ai/artifact/367eAB5hipPpN4u2fVj4Vj (privat).
