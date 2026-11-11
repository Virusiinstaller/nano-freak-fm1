#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# FMini-1: the official site, one self-contained page (site_fmini.html: the Minimoog-styled site with the screens, the presets,
# MIDI and the Felucca installer's logic; the firmware packages and every picture are embedded). The installer carries the
# FM-1's package and the SMK-37's three (Pro, Elite, MKE-P37), each for exactly its model; the device found picks it.
#   web/make_fmini_installer.py build/fmini-1-fm1-<version>.fwsc OUT.html [VERSION [SMK_VERSION]]
# The SMK-37 packages are build/fmini-1-smk37[-elite|-mke]-<SMK_VERSION>.fwsc (BOARD=smk37 [MODEL=...] ./build.sh);
# SMK_VERSION defaults to VERSION (one version for both boards, FMINI_VERSION in firmware/src/core.h).
# Serve it over https or http://localhost (Web MIDI with SysEx needs a secure context), in Chrome / Edge.
import base64
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_site import HERE, strip_module, product_of  # noqa: E402

SMK_STEMS = (("fmini-1-smk37", "SMK-37 Pro"), ("fmini-1-smk37-elite", "SMK-37 Elite"), ("fmini-1-smk37-mke", "MKE-P37"))


def package(path, want, label, version):
    raw = Path(path).read_bytes()
    product = product_of(raw)
    if not re.fullmatch(re.escape(want) + r"_9\d\d", product) or b"FELUCCA-LOADER-1" not in raw:
        raise SystemExit(f"{path}: not a Felucca-loader package for {want} (identity {product!r})")
    return {"product": product, "label": label, "file": Path(path).name, "version": version,
            "pkg": "data:application/octet-stream;base64," + base64.b64encode(raw).decode()}


def main(pkg, out, version="1.4.2", smk_version=None):
    if smk_version is None:
        smk_version = version
    build = Path(pkg).resolve().parent
    pkgs = [package(pkg, "FM-1", "FM-1", version)]
    for stem, model in SMK_STEMS:
        f = build / f"{stem}-{smk_version}.fwsc"
        if not f.exists():
            raise SystemExit(f"{f}: missing (BOARD=smk37 [MODEL=elite|mke] ./build.sh)")
        pkgs.append(package(f, model, model, smk_version))
    html = (HERE / "site_fmini.html").read_text(encoding="utf-8")
    lib = "\n".join(strip_module((HERE / f).read_text(encoding="utf-8")) for f in ("fm1pkg.js", "fm1ota.js", "fm1backup.js"))
    meta = json.dumps({"version": version, "smkVersion": smk_version, "pkgs": pkgs})
    hero = "data:image/jpeg;base64," + base64.b64encode((HERE.parent / "assets" / "fmini-installer-hero.jpg").read_bytes()).decode()
    imgs = {p.stem: ("data:image/" + ("webp" if p.suffix == ".webp" else "png") + ";base64,") + base64.b64encode(p.read_bytes()).decode() for p in sorted((HERE.parent / "assets" / "site").glob("*.*"))}
    names = re.findall(r'"([A-Z0-9 .\'/+&-]{2,12})"', (HERE.parent / "firmware" / "src" / "eng_fmini_presets.h").read_text())[:10]
    html = html.replace("/*IMGS*/{}", json.dumps(imgs)).replace("/*PRESETS*/[]", json.dumps(names))
    for mark in ("/*LIB*/", "/*META*/", "/*HERO*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"site_fmini.html must contain {mark} once")
    html = html.replace("/*LIB*/", lib).replace("/*META*/", meta).replace("/*HERO*/", hero)
    html = html.replace('<span class="ver" id="ver">v1.1.0</span>', f'<span class="ver" id="ver">v{version}</span>', 1)
    Path(out).write_text(html, encoding="utf-8")
    print(f"{out}: {len(html)} B, " + ", ".join(f"{p['product']} v{p['version']}" for p in pkgs))


if __name__ == "__main__":
    if len(sys.argv) not in (3, 4, 5):
        sys.exit(__doc__)
    main(*sys.argv[1:])
