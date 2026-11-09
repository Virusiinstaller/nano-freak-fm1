# FMini-1 on the M-Vave SMK-37 (beta)

FMini-1 also builds for the M-Vave SMK-37, a 37-key controller with the same chip as the FM-1. The FM-1 build is
unchanged; everything for the SMK-37 is behind `BOARD_SMK37`. The board layer comes from the
[fm1-to-smk37](https://github.com/genkigenki/fm1-to-smk37) kit (genkigenki/Felucca, branch `smk37`). The decisions
behind the mapping are in [smk37-port.yaml](smk37-port.yaml).

Nothing here has been tested on a real SMK-37 yet.

## Supported models

- **SMK-37 Pro**: the default build, `fmini-1-smk37-<version>.fwsc` (identity `SMK-37 Pro_900`). Also the Starrykey 37 Play.
- **SMK-37 Elite**: `fmini-1-smk37-elite-<version>.fwsc` (identity `SMK-37 Elite_900`).
- **MKE-P37**: `fmini-1-smk37-mke-<version>.fwsc` (identity `MKE-P37_900`).

They are the same hardware. Each package carries its model's name, so the manufacturer's updater still takes that
unit's own stock package later: install the package for your model. The installers take a package only for exactly
the connected model.

## Controls

| SMK-37 | FMini-1 |
| --- | --- |
| Keys F3..G5 (the middle 27) | the FM-1's 27 keys, with velocity |
| Keys C3..E3 and G#5..C6 (the outer 10) | more notes, one each, with velocity |
| 16 pads | the preset picker: the 16 presets of the page that holds the loaded one, in their category's colour (the loaded one bright); a pad loads its preset (in the SAVE dialog: the slot it writes). In the SEQ pop-up the pads show 16 of its 64 steps (the page of the step that plays, or of the slot a take writes next) |
| K1 | MASTER (volume) |
| K2 / K3 / K4 | PRESETS / SELECT / ALGORITHM |
| K5..K8 | KNOB 1..4 (the page's four knobs) |
| PATCH | HOME: the FILT page, the pop-ups close |
| PARA | EDIT: FILT; again on FILT, PRESET SETTINGS |
| SCALE | SCL: OSC, again MIX; in the delay pop-up, tap tempo |
| CHORD | ENV: F.ENV, again A.ENV |
| NOTE REPEAT | LFO: CTRL |
| FX | FX; again, the four FX pop-ups in turn |
| GLOBE | GLO: the GLOBAL pop-up |
| BT | SAVE |
| ARP / SEQ | the ARP / SEQ pop-ups |
| SEQ PLAY / SEQ REC | PLAY / REC |
| OCTAVE- / OCTAVE+ | OCT- / OCT+ |
| < / > | the previous / next preset; held 1 s, ten presets a second |
| FADER BANK | what the faders set: A the A.ENV knobs, B the F.ENV knobs, C the MIX knobs; it opens that page (lit on bank B, F.ENV) |
| Faders 1..4 | the four knobs of the bank's page; a fader takes over only once it moves |
| KNOB BANK | (free) |
| Pitch wheel | pitch bend (the preset's PITCH BEND range); also sent as MIDI |
| Mod wheel | as MIDI CC1 on the FM-1 (the vibrato); also sent as MIDI |
| OCTAVE- + OCTAVE+ held at power-on | HARDWARE CALIBRATION: teach the buttons and knobs again |

The SMK-37's TRS jack is a MIDI output and this build does not use it; there is no TRS MIDI IN, so MIDI clock comes
over USB only.

## Build

See [BUILDING.md](BUILDING.md) for the toolchain.

```sh
BOARD=smk37 ./build.sh                 # build/fmini-1-smk37-<version>.fwsc (SMK-37 Pro)
BOARD=smk37 MODEL=elite ./build.sh     # build/fmini-1-smk37-elite-<version>.fwsc
BOARD=smk37 MODEL=mke ./build.sh       # build/fmini-1-smk37-mke-<version>.fwsc
./build.sh                             # the FM-1 build again before tests/run_tests.sh (the tests use its files)
```

## Install

Make a full flash backup first (the fm1-to-smk37 kit, `docs/02-update-and-recovery.md`, section 3), and have the
manufacturer's package for your model and its updater ready. Then connect the keyboard by USB and run, with the
package for your model:

```sh
python3 tools/fm1_install.py build/fmini-1-smk37-<version>.fwsc
```

`fm1_install.py` needs `mido` and `python-rtmidi` (`pip3 install mido python-rtmidi`). It checks that the package is
for the connected model.

## Going back to the stock firmware

Install the manufacturer's firmware package for your model with the manufacturer's updater. It works directly,
because the installed package keeps the keyboard's model name.

## Safety

- The first start clears the stock firmware's settings, and any other firmware's data, in the shared data area
  (FMini-1's store is tagged `SMK37DATAFMINI`).
- FMini-1's 200 preset slots live in the user sample area (0xC2000..0xDBFFF). The stock firmware's key, knob and
  fader mapping there is kept until FMini-1 first writes a preset; the stock marker is released before that, so the
  stock firmware rebuilds its factory mapping after a return to stock.
- The firmware never writes the keyboard's calibration, Bluetooth pairing or key areas; the flash map refuses them at
  compile time and again in the flash driver.
- Installing other firmware is at your own risk.
