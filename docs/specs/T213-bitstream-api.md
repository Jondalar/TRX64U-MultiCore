# T213 - Bitstream API (a): API TOML per core, modules in the service (epic)

Status: PROPOSED 2026-11-12, Alex (T192 D24).

## §1 The problem
- T192 D24 (Alex 2026-11-12): bitstream (a) -> service with modules (b) -> client (UI). (b) is built in every core (T192.3 D31,
  `coreapp/src/uiapp.cc` `service_core`). (a) is not: every core describes itself as a C table in the app (`coreapp/src/table.cc`,
  `coreapp/cores/<core>.cc`, 19 descriptors, 13,566 lines incl. shared format code); only trx64u carries a UI TOML (menu record 6).
- Alex: "(a) will be different for every core, but should have the same function language and naming"; "(b) should be strongly
  generalised, so that the UI works from bitstream + service described declaratively only".

## §2 Scenarios
(per child spec)

## §3 Decisions
- D1 (Alex 11-12, "B"): option B - format and machine logic are C modules in the service (1541/CBM start, tape, disk/cart formats,
  machine modules such as c128/pet/neogeo); the API TOML of a core chooses and parametrises them; the TOML is no programming language.
- D2 (Alex 11-12, "subspec core by core"): one epic, children: T213.1 function language + API TOML schema + lint gate against new per-core
  C data; T213.2 the modules in the service (cut with GitNexus by who uses what); T213.3 ff. one child per core (C descriptor -> API TOML +
  modules, same pictures and tests before and after), the A1200 first (the T192 PoC core).
- D3 (Alex 11-12, "all of that in, yes"): T212 ships in the test release as built; its per-core fields (drive lists, info line, task
  groups) move to the API TOML with T213.

- D4 (Alex 2026-11-16, after the coordinator found 1322 lines of new per-core C in coreapp/cores since D24 and the Port Diagnostics
  tests embedded in every core's image, translated: "Damn, we had clearly settled in the app architecture that everything core-specific
  goes into libs or APIs. Where did that go wrong again!!!!!"; on the proposed lint gate: "Do it. It is better that way."): the lint
  gate of T192 D24 is built now, before the release: a ratchet over a ledger of today's per-core code in the app (coreapp/cores/* and
  the core-specific files of coreapp/src); a new per-core file, a grown ledger file, a new core name/id in the generic code or core data
  embedded in the app image makes the lint red, unless an exception line with spec reference and Alex's decision is in the ledger.
  Only a lint: no migration is forced (T213 stays after the release). Every agent brief cites D24 from now on.

## §4 Out
- Gideon's and Commodore's C64 firmware (T192 D22).

## §5 Build notes
