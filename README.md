# DR32 — Drum Rack 32

**Ableton Move's Drum Rack, with 32 pads instead of 16.**

### 📖 [Read the manual →](https://legsmechanical.github.io/schwung-dr32/manual.html)

> **Install** from Schwung Manager once [the catalog entry](https://github.com/charlesvestal/schwung/pull/484)
> merges; until then, from the [v0.3.0 release](https://github.com/legsmechanical/schwung-dr32/releases/tag/v0.3.1).
> **Requires [Schwung](https://github.com/charlesvestal/schwung) 1.3.0 or later** — that is where
> the module bus framework landed, and DR32's per-pad routing and sends depend on it.

DR32 loads Move's own drum kits — the same `.ablpreset` files the hardware makes — and plays them
through a reconstruction of the same Drum Sampler voice. A kit you built on the device opens here
and **sounds like itself**, with twice the pads.

It is a Schwung sound generator: drop it into a chain slot like any other instrument.

---

## What you get

**32 pads, notes 36–67.** Hit a pad and the editor follows it. The kit you load fills as many as it
has; the rest stay empty and ready.

**A kit browser that only offers kits that work.** Move's preset folder holds presets for every
instrument — on a typical device, several hundred files of which only a fraction are drum racks.
DR32 reads them and lists the real ones, grouped the way the library groups them: Acoustic,
Electronic, Hybrid, and everything you've made under **My Kits**. Scrolling the list auditions what
you land on, so you can hear your way through a category.

**Per-pad sound editing**, the way the hardware does it: playback region, transpose and detune,
choke groups, two envelope modes (A-H-D and A-S-R), four filter types, velocity response, pan,
level and Punch. Pad names follow the sample, so the header tells you which drum you're on.

**Sample swapping without leaving the page.** **SMPL** opens a browser already in the folder that
pad's sample came from; **BRWS** steps through its neighbours one at a time, so you can try the
other snare without opening anything.

**Route pads to their own buses, with their own effects.** DR32 publishes all 32 pads to Schwung's
**module bus** framework, so the host can group any subset of them onto a bus that renders into its
own buffer and carries **its own chain of insert effects** — hats through a reverb while the kick
stays dry, or the whole top end through a compressor without touching the low drums. You build the
groups in Schwung, not in DR32; the pads arrive named after the samples in the loaded kit, so you
are picking *Kick 707* rather than *voice 3*. Pad identity is stable, so a routing you set up
survives loading a different kit.

**And two sends per pad, into the global returns.** Independently of any bus, every pad has its own
Send A and Send B amount in dB — exactly what a send amount means in a Move kit, so an imported kit
arrives with its levels intact. Put any effect on a return and the whole kit can tap it at its own
level: one reverb for the drums, not one inside each pad.

> Buses **group**, sends **tap**. A bus is where a set of pads goes; a send is how much of a pad
> goes somewhere shared. DR32 carries no effects of its own — all of this is the host's, which is
> why any Schwung effect module works here.

**Link** sets one control across all 32 pads at once. Arm it, sweep a knob, and every pad takes that
value — then it releases the moment you change *anything* else: another knob, a sample, a browse
step, the master level. So the next thing you touch is that pad's alone. Playing a pad doesn't end
it, so you can audition while you sweep. It never spreads the things that make a pad a distinct pad:
its sample, its note, its browse position.

## The pages

Jog moves between them.

| | |
|---|---|
| **Category** | Acoustic · Electronic · Hybrid · My Kits — click one to open its kits |
| **Kit** | the kits in that category |
| **Sample** | Pad · Sample · Start · End · Transpose · Detune · Choke · Browse |
| **Shape** | Attack · Decay · Hold · Envelope · Cutoff · Reso · Type · Filter |
| **Mix** | Vel Vol · Volume · Pan · Link · Send A · Send B · Punch · Punch Time |
| **Master** | level for the whole kit |

**Sample, Shape and Mix are three views of one pad** — pick the pad on any of them and it's picked
on all three.

There's no custom interface to learn: every page is Schwung's own knob grid, so the sample waveform,
the envelope, the filter curve and the pad map all draw the way they do everywhere else. Help is on
the device too, under Module Help.

## Good to know

- 🔴 **Browsing kits has no undo.** Moving the cursor loads what you land on, and the kit you were on
  is gone. Save your work before you go shopping.
- **Pads 17–32 sit outside Move's 4×4 pad map**, so the small map in the header doesn't light for
  them.
- **Your edits live in the Schwung set**, on top of the kit that was loaded — so a set remembers
  which kit you used and what you changed. The kit files themselves are Move's, and are never
  modified.
- **Per-pad playback effects are not played back.** They're read and written back untouched, so kits
  stay lossless and reopen correctly on Move; DR32 just plays the plain sampler.
- **Return-chain effects saved inside a kit are preserved but not set up for you** — a kit's send
  *amounts* are imported, but what sits on the return is yours to choose.

## Credits and licence

MIT. Built on [Schwung](https://github.com/charlesvestal/schwung) by Charles Vestal.
Not affiliated with Ableton.

The drum engine is a **reconstruction, not a design** — its behaviour comes from analysis of and
measurement against Move's own, which means any audible deviation is a bug even when it sounds
nicer.

---

## For developers

```sh
./scripts/build.sh          # cross-compiles the DSP for the device (Docker)
./scripts/install.sh        # deploys and ALWAYS restarts the stack
tests/run.sh                # off-device suite
node tools/pages_check.mjs  # upstream's validator over the SERVED hierarchy
```

⚠️ A restart is mandatory after every deploy — swapping the synth out and back in is not enough, and
a skipped restart looks exactly like a deploy that did nothing. `install.sh` handles it.

⚠️ If `build.sh` fails, `dist/` keeps the previous build and `install.sh` will happily ship it. Check
for `==> done:` before trusting an install.

| path | |
|---|---|
| `dsp/` | the DSP: voice, kit loader, `.ablpreset` parsing, state |
| `src/` | `module.json` (the served hierarchy), `help.json`, the play-view `ui.js` |
| `lib/` | `.ablpreset` reading/writing |
| `tests/` · `tools/` | the off-device suite and the validators |
| `docs/` | the [manual](https://legsmechanical.github.io/schwung-dr32/manual.html) and design notes |

The conventions, and the traps worth knowing before changing anything, are in
[`CLAUDE.md`](CLAUDE.md).
