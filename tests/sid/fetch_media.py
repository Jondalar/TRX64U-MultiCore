#!/usr/bin/env python3
"""Fetches the test media of the SID suite into tests/sid/media/ (git-ignored). Only freely downloadable things.

  VICE testprogs SID/envelope + SID/waveforms   svn export from sourceforge (GPL; .asm and .prg)
  csdb                                          the releases below: one request every 12 s, direct file links from the
                                                webservice (the counting download.php link only for the issue's own link)
  LAMENOTER                                     from Alex's zip (~/Downloads, U64/PCM pair only)
What cannot be fetched is listed at the end; nothing is faked.
"""
import os, re, shutil, subprocess, sys, time, urllib.parse, urllib.request, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
M = os.path.join(HERE, "media")
UA = "Mozilla/5.0 (Macintosh) Safari/605.1"
# (dir, csdb release id, what)
RELEASES = [
    ("pigquest", 229717, "A Pig Quest V1.02 (EasyFlash CRT, intro speech 'Piggy 18')"),
    ("planetgolf", 157572, "Planet Golf (crack of the 2017 game)"),
    ("bozuma", 126031, "Bozuma - The Mystery of the Mummy (crack)"),
    ("lastv8", 115132, "The Last V8 (crack)"),
]
missing = []


def get(url, out=None):
    time.sleep(12)
    req = urllib.request.Request(url.replace(" ", "%20"), headers={"User-Agent": UA})
    data = urllib.request.urlopen(req, timeout=60).read()
    if out:
        open(out, "wb").write(data)
    return data


def unpack(path, dest):
    low = path.lower()
    if low.endswith(".zip"):
        zipfile.ZipFile(path).extractall(dest)
    elif low.endswith(".rar"):
        subprocess.run(["unrar", "x", "-o+", "-inul", path, dest + "/"], check=True)
    elif low.endswith((".7z", ".lha", ".lzh")):
        subprocess.run(["7z", "x", "-y", "-o" + dest, path], check=True, capture_output=True)
    else:
        return False
    os.remove(path)
    return True


def main():
    os.makedirs(M, exist_ok=True)
    for d in ("envelope", "waveforms"):
        dst = os.path.join(M, "vice", d)
        if not os.path.isdir(dst):
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            r = subprocess.run(["nice", "-n", "15", "svn", "export", "-q",
                                "https://svn.code.sf.net/p/vice-emu/code/testprogs/SID/" + d, dst])
            if r.returncode:
                missing.append("vice/" + d)
    nl = os.path.join(M, "nextlevel")
    if not os.path.exists(os.path.join(nl, "Insomnia.sid")):
        os.makedirs(nl, exist_ok=True)
        try:
            get("https://csdb.dk/release/download.php?id=286305", os.path.join(nl, "nextlevel.zip"))
            unpack(os.path.join(nl, "nextlevel.zip"), nl)
        except Exception as e:
            missing.append("nextlevel (%s)" % e)
    for d, rid, what in RELEASES:
        dst = os.path.join(M, d)
        if any(not f.endswith(".xml") for f in (os.listdir(dst) if os.path.isdir(dst) else [])):
            continue
        os.makedirs(dst, exist_ok=True)
        try:
            xml = get("https://csdb.dk/webservice/?type=release&id=%d&depth=2" % rid).decode("utf-8", "replace")
            links = [l.replace("&amp;", "&") for l in re.findall(r"<DownloadLink><Link>([^<]+)</Link>", xml)]
            if not links:
                raise RuntimeError("no download link in the release data")
            name = urllib.parse.unquote(os.path.basename(urllib.parse.urlparse(links[0]).path))
            target = os.path.join(dst, name)
            get(links[0].replace("http://", "https://"), target)
            unpack(target, dst)
        except Exception as e:
            missing.append("%s (csdb %d: %s)" % (d, rid, e))
    lm = os.path.join(M, "lamenoter")
    z = os.environ.get("LAMENOTER_ZIP", os.path.expanduser("~/Downloads/LAMENOTER-Breadbox-Season-U64-16MB-REU.zip"))
    if os.path.exists(z) and not os.path.exists(os.path.join(lm, "lamenoter-u64.prg")):
        os.makedirs(lm, exist_ok=True)
        with zipfile.ZipFile(z) as zf:
            for n in zf.namelist():
                if n.endswith((".prg", ".reu", ".txt")):
                    with zf.open(n) as src, open(os.path.join(lm, os.path.basename(n)), "wb") as dst:
                        shutil.copyfileobj(src, dst)
    if not os.path.exists(os.path.join(lm, "lamenoter.prg")):
        missing.append("lamenoter.prg + breadbox.reu (the 8-bit SID digi pair; Alex's zip holds only the U64/PCM pair)")
    print("media in", M)
    for f in sorted(os.listdir(M)):
        print("  ", f, sorted(x for x in os.listdir(os.path.join(M, f)) if not x.endswith(".xml"))[:12] if os.path.isdir(os.path.join(M, f)) else "")
    if missing:
        print("NOT fetched:")
        for m in missing:
            print("  -", m)


if __name__ == "__main__":
    main()
