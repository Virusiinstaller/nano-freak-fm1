#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""FMini-1 on the M-Vave SMK-37 (beta): the installer page (site_smk37.html), one self-contained file with the three
SMK-37 packages of a version embedded (SMK-37 Pro, Elite, MKE-P37). The page reads the connected keyboard's model and
writes that model's package.
  web/make_smk_installer.py VERSION OUT.html     (takes build/fmini-1-smk37[-elite|-mke]-VERSION.fwsc)
Serve it over https or http://localhost (Web MIDI with SysEx needs a secure context), in Chrome / Edge."""
import base64
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_site import HERE, strip_module, product_of  # noqa: E402

BUILD = HERE.parent / "build"


def main(version, out):
    boards = []
    for stem in ("fmini-1-smk37", "fmini-1-smk37-elite", "fmini-1-smk37-mke"):
        f = BUILD / f"{stem}-{version}.fwsc"
        if not f.exists():
            raise SystemExit(f"{f}: missing (BOARD=smk37 [MODEL=elite|mke] ./build.sh)")
        raw = f.read_bytes()
        prod = product_of(raw)
        if not re.fullmatch(r"(SMK-37 Pro|SMK-37 Elite|MKE-P37)_9\d\d", prod) or b"FELUCCA-LOADER-1" not in raw:
            raise SystemExit(f"{f}: not an SMK-37 package of FMini-1 (identity {prod!r})")
        boards.append({"product": prod, "file": f.name,
                       "pkg": "data:application/octet-stream;base64," + base64.b64encode(raw).decode()})
    html = (HERE / "site_smk37.html").read_text(encoding="utf-8")
    lib = "\n".join(strip_module((HERE / f).read_text(encoding="utf-8")) for f in ("fm1pkg.js", "fm1ota.js"))
    for mark in ("/*LIB*/", "/*META*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"site_smk37.html must contain {mark} once")
    html = html.replace("/*LIB*/", lib).replace("/*META*/", json.dumps({"version": version, "boards": boards}))
    Path(out).write_text(html, encoding="utf-8")
    print(f"{out}: {len(html)} B; {', '.join(b['product'] for b in boards)}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(*sys.argv[1:])
