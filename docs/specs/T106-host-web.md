# T106 — the host application answers a browser, and answers us in JSON

**Status:** IN PROGRESS (2026-10-16). Alex, 2026-10-16: "Ggfs sollten wir auch ein Browser Menü
erlauben noch? Oder Rest api in der Host App." — and, asked whether to build it: "Ja mach."

---

## §1 The problem

**Who:** the person at a board whose screen shows nothing, and us, running board tests without a
keyboard.

**What happened.** T105 fixed the cause of one black screen - the mode was hard-coded - but it did not
change the shape of the trap: **a board that shows nothing cannot be told anything.** The file browser
lists only `.cor`, so the updater cannot be started to install a cure; the boot menu is empty after a
fresh install, so nothing runs by itself; and the keyboard only helps somebody who can see what they
are doing. The one tester who hit it got out by moving the HDMI cable to a television. That is luck,
not a path.

The board is not silent, though. The host application already runs lwIP and **an FTP server on port 21**
(`host-app/software/network/ftpd.cc`, any user name, empty password unless one is set). So a second way
in exists in principle; what is missing is something a person can use from a phone, and something a
test can use without pretending to be a keyboard.

**And a measurement that stops us from overselling it:** the tester's board showed **`no network`** in
the top bar while its ethernet lights were on. Whatever this becomes, it must be the second way in, not
the first, and the application must boot exactly as it does today when nothing answers.

**Parts (MECE):**
- **P1 See:** a browser on the same network shows what the board sees - menu, media, bundles, picture.
- **P2 Act:** from that page: start an entry, add or remove one, change the picture.
- **P3 Machine-readable:** the same facts and the same actions as JSON, for tests and for us.
- **P4 Harmless:** no web server failure can keep the board from booting, and the page never becomes
  the only way to do anything.

## §2 Scenarios

```gherkin
Scenario: S1 the page shows what the menu shows (P1)
  Given a boot menu with two entries (2026-10-25, T101 S10: there is no "found bundle" list and no add route any more)
  When  GET / is answered
  Then  both entry names are in the page, and no "Found on the media" section
  And   so are the current picture mode and the media the board sees
  Bench: tests/hostapp/test_webui.cc against a fake state

Scenario: S2 the picture can be changed from the page (P2)
  When  POST /v1/video with sys=1 and hdmi=4 arrives
  Then  the command queue holds exactly that change
  And   the UI task applies it, writes boot.cfg and the page shows the new value
  Bench: tests/hostapp/test_webui.cc; control: a route that ignores the form body must fail it

Scenario: S3 an entry can be started (P2)
  When  POST /v1/boot with index=1 arrives for a startable entry
  Then  the queue holds a start for entry 1
  And   an index that is not startable is refused with 409 and nothing is queued

Scenario: S4 the same facts as JSON (P3)
  When  GET /v1/status is answered
  Then  it parses as JSON and names the host key, the app version, the picture, the media,
        every menu entry with its state (no `found` array since 2026-10-25)
  Bench: tests/hostapp/test_webui.cc parses the answer

Scenario: S5 nothing it does can stop a boot (P4)
  Given the network never comes up
  Then  the boot menu behaves exactly as it does without this feature, and the countdown still runs
  Bench: tests/hostapp/render/render.cc (unchanged expectations) and the board step

Scenario: S6 one writer (P4)
  Given a POST arrives while the user is moving the selection
  Then  the web task only queues, and the UI task is the only one that touches the menu
  Bench: tests/hostapp/test_webui.cc (the queue is the whole interface)
```

## §3 Decision

- **D1 Gideon's server, our routes.** `server.c` and `http_protocol.c` from the MicroHTTPServer copy
  the submodule already carries (`host-app/software/httpd` is a symlink, the include path is in
  `environment.mk`). Not `middleware.c`: its `Dispatch` is wired to his `execute_api_v1`, which drags
  in his whole application API. We pass our own callback to `HTTPServerRun`. Same rule as everywhere
  else: his code is a library, not an application (T104 D1).
- **D2 One writer.** The web task never touches the boot menu, the settings or the video mode. It puts
  a command in a one-slot mailbox and answers 202; the UI task picks it up on its next pass, applies it
  and records the result. Reads are served from a snapshot the UI task refreshes. No locks, no second
  writer, and the UI stays the thing that decides.
- **D3 Plain forms, no JavaScript.** The page is HTML a phone browser renders without help, and every
  action is a form POST. A page that needs scripting is a page that fails when it is needed.
- **D4 The JSON is the same data, not a second truth.** `/v1/status` is the snapshot D2 keeps; the page
  renders that same snapshot. One source, two spellings.
- **D5 No authentication, and said out loud.** The FTP server beside it is already open with no
  password, on a private network. Adding a login to one of the two and not the other would be theatre.
  INSTALL.md says the board answers on the network and what that means.
- **D6 Off by a missing network, never by a crash.** The task starts only after the stack has an
  address, retries, and a failure to bind is a log line, not a stop.

## §4 Out

- **The core application.** T93 D12 (REST in the blueprint) stays open and is not this spec; a core
  that wants it gets it through the same routes later.
- **Uploads.** Copying `.cor` files and ROMs onto the board is what the FTP server is for.
- **Authentication, TLS, accounts** (D5).
- **A full file browser in the page.** It lists what the board found; walking directories is FTP's job.

## §5 Decisions taken while building

### 5.1 What MicroHTTPServer actually hands over (2026-10-16)

Three things in it decided the shape of the code, and none of them is in its README.

- **The application writes the whole response**, status line and headers included, into `res->_buf`;
  `server.c:157` sends those bytes raw. `middleware.c:82` and `dummy_api.c:69` do it the same way.
- **`HTTP_BUFFER_SIZE` is 1024** (`server.h:13`), so a page of ten kilobytes cannot be handed over in
  one piece. It goes out through `res->BodyCB`, which `WriteSock` calls for each next chunk
  (`server.c:149-157`). `web_page()` renders into one buffer and the callback pays it out.
- **A POST body arrives through a callback, not as a pointer.** The dispatch is called once with the
  header - "early call to processor to set up body absorber" (`http_protocol.c:375`) - installs
  `req->BodyCB`, and the answer is written when the terminate arrives. A POST whose form rode in the
  query string has `bodyType == eNoBody` and is answered straight away.

One absorber, so one POST at a time; a second while one is in flight is answered 503 rather than mixed
into the first. This is a rescue page, not a service.

### 5.2 The add path is shared, not copied

`add_found_row()` was the A key's: it read the selection, checked the bundle and added it. The network
needed the same by index, so the check moved into `add_found_at(i, &at)` and the key calls that. A
bundle refused for the keyboard - another contract, another host, a bad file - is refused for the
browser with the same words. Proven in the bench: `WEB_ADD` on the contract-2 bundle does not add it.
