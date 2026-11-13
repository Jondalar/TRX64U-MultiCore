# T158 — the generic core application browses remote FTP servers like a medium, as the Ultimate application does

**Status:** PROPOSED (2026-10-29), asked by Alex. Status check 2026-11-13: PROPOSED stays, but an FTP medium already exists in the core app (T104.2 9da495bdb: netfs.cc, ftp_client.cc, settings 'FTP n ...'; T186 fixes it); Q1-Q3 were never answered here, URL entry and §2 proofs not checked.
**Repo:** TRX64U-MultiCore — the generic core application (`coreapp/src`: a file system behind the Library browser, the
server list in its settings). Host and core gateware are not changed.
**Number:** T158.
**Origin:** Alex, 2026-10-29 (translated): "for the enhancements of the generic app: an FTP client incl. URL (and if needed
credentials) ... like Gideon's app. Own todo."
**Depends on:** T104 (generic app), T104.2 (the Library's media level: a server shows there beside SD / Flash / USB1).
**Milestone:** M3.

---

## §1 The problem

- **Who:** a person in a core (Amiga, Apple II, Atari, ...) whose images lie on a NAS or PC, not on the stick.
- **Today:** the generic app's Library sees only the local media (SD, Flash, USB). Images have to be copied to the stick
  first (FTP upload to the board, which is limited: `host-app/software/network/ftpd.cc:29`, 4 sessions).
- **The model** (read from the code, not built here): the Ultimate application of the fork (`apps/ultimate`) has a remote FTP
  file system: `software/filesystem/filesystem_ftp.{h,cc}` (753 lines) on `software/network/ftp_client.cc` (508 lines). A
  server entry has alias, host, port, user, password, folder (`filesystem_ftp.h:176-186`), kept as JSON (`ftp_servers`),
  edited and removed from the browser (`filesystem_ftp.h:65-66`); a file is downloaded to `/Temp/` on first open, a written
  file is uploaded on close (`filesystem_ftp.cc:88-92, 140-187`).
- **Parts (MECE):**
  - **P1 Servers:** add / edit / remove a server by URL (`ftp://[user[:password]@]host[:port]/folder`) or by fields; the list is
    kept (credentials stored as in the Ultimate application, decision Q2).
  - **P2 Browse:** a server shows as a medium on the Library's media level (T104.2) and is browsed like a folder (list, sizes).
  - **P3 Use:** mount / run an image from the server: downloaded to a local cache first, then used as a local file.
  - **P4 Write back:** a written image (save, write-back) goes back to the server (upload on unmount / close), or the image
    is read only (decision Q3).
  - **P5 Failure:** no network, wrong login, server gone: a message, never a hang; the core keeps running.

## §2 Scenarios

```gherkin
Scenario: S1 add a server by URL and browse it
  Given the generic app with network and an FTP server ftp://user:pw@host/images
  When  the server is added by its URL
  Then  it shows on the Library's media level, RETURN lists /images with names and sizes
  # tests/coreapp with a fake FTP server (control: wrong PASV parsing -> FAIL); ue2emu walk; board step

Scenario: S2 run an image from the server
  Given a .d64 / .adf / .dsk on the server
  When  it is mounted from the browser
  Then  it is downloaded to the cache once and mounted from there; a second mount does not download again

Scenario: S3 write back
  Given an image from the server, written by the guest (Q3 = upload)
  When  it is unmounted
  Then  the server holds the written image (size and checksum)

Scenario: S4 failure
  Given the server down / a wrong password
  Then  a message within 10 s, the browser stays usable, the core runs on
```

## §3 Decision

Open, see §3a.

### §3a Open questions (recommendation first)

1. **Q1 Port or rewrite:** port `filesystem_ftp` + `ftp_client` from the fork into the generic app's file-system layer
   (recommended: proven code, 1:1 behaviour) vs. a new small client.
2. **Q2 Credentials:** stored in the app's config like the Ultimate application (plain, on the stick) vs. asked at connect.
3. **Q3 Write back:** upload on unmount (as the Ultimate application) vs. server images read only.

## §4 Out

- FTPS / SFTP, other protocols (SMB, HTTP).

## §5 Decisions taken while building
