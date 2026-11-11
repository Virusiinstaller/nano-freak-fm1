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
| 16 pads | the preset picker: the 16 presets of the page that holds the loaded one, dim blue (the loaded one bright white); a pad loads its preset (in the SAVE dialog: the slot it writes). In the SEQ pop-up the pads show 16 of its 64 steps (the page of the step that plays, or of the slot a take writes next): a step with a note in its window's colour (1-16 blue, 17-32 red, 33-48 yellow, 49-64 green), the step that plays white, the others dark |
| K1 | MASTER (volume) |
| K2 / K3 / K4 | PRESETS / SELECT / ALGORITHM (the screen's hints name them K2, K3, K4) |
| K5..K8 | four knobs, by KNOB BANK: FILT's (the default), CTRL's (LFO), FX's, or the four of the page on the screen (the FM-1's K1..K4); turned, the screen shows their page for 1 s, then the page before comes back (and stays the Screen Knobs' page meanwhile) |
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
| < / > | the previous / next preset, loaded when the button is let go; held 0.66 s, they run on ten a second (the pop-up shows where) |
| FADER BANK | what the faders set: A.ENV (the default), F.ENV, MIX, or the page on the screen; a pop-up names the bank for 1.2 s; lit off the default |
| Faders 1..4 | the four knobs of the bank's page; a fader takes over only once it moves, and shows its page for 1 s as K5..K8 do |
| KNOB BANK | what K5..K8 set: FILTER (the default), LFO, FX, or the page on the screen; a pop-up names the bank for 1.2 s; lit off the default |
| Pitch wheel | pitch bend, by the preset's PITCH BEND range (PRESET SETTINGS); also sent as MIDI |
| Mod wheel | as MIDI CC1 on the FM-1 (the vibrato); also sent as MIDI |
| OCTAVE- + OCTAVE+ held at power-on | HARDWARE CALIBRATION: teach the buttons and knobs again |

## MIDI

The SMK-37's TRS jack is a MIDI output; there is no TRS MIDI IN, so MIDI comes in over USB only (MIDI clock too).
GLO > MIDI SETTINGS on the SMK-37:

| Line | Options |
| --- | --- |
| MIDI OUT | ALL (USB and the TRS jack, the default), USB, TRS |
| MIDI OUT CH | 1..16 (default 1): the keys, the wheels and the ARP and SEQ notes |
| MIDI IN CH | ANY (the default), 1..16 |
| MIDI IN CC | the CC of each control |
| MIDI CLOCK | OFF, USB |
| MIDI IN MONITOR | the last messages in |

What MIDI OUT sends (USB and / or TRS, as MIDI OUT says):

- the 37 keys: their notes with velocity, on MIDI OUT CH, with the octave (OCTAVE- / OCTAVE+);
- the 16 pads: notes 36..43 (the bottom row) and 44..51 (the top row) on channel 10, with velocity and note-off; they
  still pick presets;
- K1..K8: CC 102..109 on MIDI OUT CH, whatever they do in FMini-1 (each encoder keeps a value 0..127 from 64; K1 is
  the MASTER's 0..127);
- faders 1..4: CC 110..113 on MIDI OUT CH, whatever bank they are in;
- the pitch wheel (pitch bend) and the mod wheel (CC1).

The TRS output follows the fm1-to-smk37 kit's driver pattern (UART1 TX on PC7) and has not been tried on a unit. The
jack is said to be type A; if a synth does not answer, try a type B adapter.

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
