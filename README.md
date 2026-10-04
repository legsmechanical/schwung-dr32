# DR32 — Drum Rack 32

**Ableton Move's Drum Rack, with 32 pads instead of 16.**

### 📖 [Read the manual →](https://legsmechanical.github.io/schwung-dr32/manual.html)

> **Install** from Schwung Manager (it is in the catalog as *DrumRack32*), or from the
> [releases page](https://github.com/legsmechanical/schwung-dr32/releases).
> **Requires [Schwung](https://github.com/charlesvestal/schwung) 1.5.0 or later.** That is where a
> synth pad's pages learned to follow the pad, the Resample page became enterable, and the envelope
> picture learned DR32's two envelope modes.

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
Electronic, Hybrid, and everything you've made under **My Kits**, plus **Init**, which puts DR32 back
the way it opens. Scrolling the list auditions what you land on, so you can hear your way through a
category. The kit is read in the background, so auditioning never interrupts the rest of your set.

**Per-pad sound editing**, the way the hardware does it: playback region, transpose and detune,
choke groups, two envelope modes (Trigger and Gate, as on Move's Drum Sampler) with envelope knobs weighted for fine short times, four filter types, velocity response, pan,
level and Punch. Pad names follow the sample, so the header tells you which drum you're on, and the
PAD knob shows the pad's number in large type. Drums are seated by the note that plays them, so the
number you see is the pad you hit.

**Any pad can be a synthesised drum instead.** The **ENGN** knob opens a picker with a section per
engine: **Sample** (the Move and User libraries, as always), **Simian**, **Urchin**, **9W9**, **6W6**,
**8W8**, **CW-78**, **ChowKick** and DR32's own **FM** (four engines: Kick, Snare, Metal and Perc). Pick a drum, such as *Kick*, *Closed Hat* or *Rimshot*, and that pad stops
being a sampler and plays that engine's voice. Mix and match freely: a sampled kick, an 8W8 cowbell,
a Simian clap and an Urchin ride can sit in one kit. Everything around the voice stays DR32's: volume, pan, velocity, choke groups, the sends,
Link, Copy and Delete, and the per-pad buses all treat a synth pad the same as a sample pad.

| engine | what it is | its pages |
|---|---|---|
| **Simian** | [OneTrick SIMIAN 2](https://punklabs.com/ot-simian)'s voice: a tuned oscillator that morphs from triangle to cymbal, resonant-filtered noise and a click, all swept by one envelope. The early-80s electronic drum. | Tone · Noise |
| **Urchin** | OneTrick URCHIN's physically modelled drums: a struck shell with a resonant head for kicks, toms and snares (with Rim), and a bank of inharmonic partials for hats and cymbals (with Closed, the hat pedal) | Drum · Shell · Chop · Media, or Cymbal · Chop · Media |
| **9W9** | [9W9](https://github.com/athousanddetails/schwung-9W9)'s TR-909 style voices: circuit-modelled kick, snare, toms, rim and clap, and the sampled hats, ride and crash, as on the real machine. 11 drums | Voice |
| **6W6** | [6W6](https://github.com/athousanddetails/schwung-6W6)'s TR-606 style voices, built on AudioKit's 606-Inspired-Synth-Drums. 8 drums | Voice |
| **8W8** | [8W8](https://github.com/athousanddetails/schwung-8W8)'s TR-808 style circuit models, congas, claves, maracas and cowbell included. 16 drums | Voice |
| **CW-78** | [CW-78](https://github.com/athousanddetails/schwung-cw-78)'s CR-78 style voices, modelled from the service notes: bongos, guiro, tambourine and metal beat among them. 14 drums | Voice |
| **ChowKick** | [ChowKick](https://github.com/Chowdhury-DSP/ChowKick), Chowdhury DSP's kick synth: a pulse shaped by a modelled diode circuit, rung through a nonlinear resonant filter. Its five factory presets are the models | Pulse · Body · Noise |
| **FM** | DR32's own FM drums, after the idea of the Machinedrum's EFM machines: a swept sine carrier, a modulator with feedback, and a filtered noise layer. Four engines, each with knob ranges sized for its drums: **FM Kick** (Kick, Tom), **FM Snare** (Snare, Clap, Rim; its noise can fire repeated Bursts), **FM Metal** (hats, cymbal, cowbell, from three cross-modulated operator pairs) and **FM Perc** (wide ranges plus a Mangle page for strange sounds). How hard you hit can move pitch, sweep, decay, FM depth and noise. 14 drums | Tone · FM (Metal) · Noise · (Mangle) · Output · Velocity |

Each drum starts from the engine's own factory values and is yours to change from there. The pages
follow the pad: hit a Simian pad and you get its Tone and Noise pages, hit a sample pad and you get
Shape. A synth pad has no Start/End, Shape or Punch, because those belong to the sampler.

The four drum machines (9W9, 6W6, 8W8, CW-78) each give a pad **one lane of the machine**, played
exactly as the machine plays it, sample for sample. The page is the machine's own panel for that
drum: **Tune**, **Decay**, **Drive** and **Distortion** on every drum, **Velocity** (how far a soft
hit falls below a hard one, and on most drums how it changes the sound), and the drum's own extra
knob where it has one, such as Attack, Snappy, Noise or Rate. The knobs run 0–127, like the
machines' own. What the machines have around their drums (reverb, delay, master drive, the
CW-78's rhythm player) is not here: DR32's Mix page and sends do that job.

**A Stereo page for every pad.** Three ways to widen a drum: **Comb** spreads it without moving it
off its pan position and leaves a mono mix-down untouched, **Haas** delays one side for more
character (the sound leans toward the side that arrives first), and **Disperse** is a smooth,
mono-safe spread. A crossover keeps the low end in the middle, so a kick's top can go wide while its
body stays put.

**Resample a pad into a sample.** The Resample page captures what a pad plays (a synth drum and
all its knobs, or a sample with its envelope and filter) as a 24-bit WAV in your User Library, and
puts it back on the pad, level-matched. **Resample Kit** does every synth pad at once.

**Sample swapping without leaving the page.** On a sample pad, **ENGN** opens straight into the folder
that pad's sample came from, with the cursor on it. Scrolling puts each sample on the pad as you land
on it, so you can hit the pad to hear the next snare along, and clicking one takes it and closes. A
sample you pick plays to its end (Trigger with Hold at Inf); turn Hold down for a shorter hit.

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

**Copy a pad onto another by hand.** Hold **Copy** and hit a pad — that one is the source; every
pad you hit while you keep holding is pasted into, sample and all. Hold **Delete** and hit pads to
clear them back to defaults. **Undo** puts back the last pad you overwrote. It's the oldest gesture
a drum rack has, and it works off the pads themselves rather than a menu, so building a kit from
one good snare is a couple of seconds of holding a button.

What travels is the whole pad — its sample first, then the tuning, envelope, filter, level, pan,
sends and Punch — not just what happens to be on the page you're looking at. What doesn't travel is
what makes a pad *that* pad: its note, and where it sits in its own sample folder.

**Link** sets one control across all 32 pads at once. Arm it, sweep a knob, and every pad takes that
value — then it releases the moment you change *anything* else: another knob, a sample, a browse
step, the master level. So the next thing you touch is that pad's alone. Playing a pad doesn't end
it, so you can audition while you sweep. It never spreads the things that make a pad a distinct pad:
its sample, its note, its browse position.

## The pages

Jog moves between them.

| | |
|---|---|
| **Category** | Init · Acoustic · Electronic · Hybrid · My Kits — click one to open its kits |
| **Kit** | the kits in that category |
| **Pad** | Pad · Engine · Start · End · Transpose · Detune · Choke · Volume |
| **Shape** | Attack · Decay · Hold · Envelope · Cutoff · Reso · Type · Filter *(sample pads)* |
| **Tone · Noise** | the Simian voice *(Simian pads)* |
| **Drum · Shell · Chop · Media / Cymbal · Chop · Media** | the Urchin voice, and its record: Vinyl/Tape noise, Sat, Rate, Bits *(Urchin pads)* |
| **Voice** | Tune · Decay · the drum's own knob · Drive · Distortion · Velocity *(9W9, 6W6, 8W8, CW-78 pads)* |
| **Tone · FM · Noise · Output · Velocity** | FM: the carrier (Level, Pitch, Decay, and Sweep and Sweep Decay except on FM Metal), the modulator (Ratio, Mod, Mod Decay, Feedback, Track), the noise (Level, Decay, Freq, Res, Filter; Bursts and Burst Gap on FM Snare and FM Perc), the output (Tone<>Noise, Drive, Low Cut, High Cut) and what velocity moves (a full-velocity hit is always the knobs as set). FM Metal's second page is *Metal*; FM Perc adds *Mangle* (Noise FM, Ring, Crush, Bits) *(FM pads)* |
| **Pulse · Body · Noise** | ChowKick: the pulse (Width, Amp, Decay, Sustain, Vel Sense, Tone), the resonant body (Frequency, Q, Damping, Tight, Bounce, Mode, Portamento) and envelope-following noise *(ChowKick pads)* |
| **Mix** | Vel Vol · Volume · Pan · Link · Send A · Send B · Punch · Punch Time |
| **Stereo** | Mode (Comb · Haas · Disperse) · Wide · Crossover · Time · Comp · Late *(Late: Haas only)* |
| **Master** | level for the whole kit |
| **Resample** | Resample Pad · Resample Kit — click in, then pick one |

**The pad pages are all views of one pad.** Pick the pad on any of them and it's picked on all of
them. Which ones you see depends on what the pad is: a sample pad shows Pad, Shape and Mix, and a
synth pad swaps Shape for its engine's pages. Start/End and Punch appear only on sample pads.

There's no custom interface to learn: every page is Schwung's own knob grid, so the sample waveform,
the envelope, the filter curve and the pad map all draw the way they do everywhere else. Help is on
the device too, under Module Help.

## Good to know

- 🔴 **Browsing kits has no undo.** Moving the cursor loads what you land on, and the kit you were on
  is gone. Save your work before you go shopping.
- **A sample you pick plays to its end.** Picking one sets the pad's envelope to Trigger with Hold at
  Inf, so a tap plays the whole file. Turn Hold down on the Shape page for a shorter hit.
- **Pads 17–32 sit outside Move's 4×4 pad map**, so the small map in the header doesn't light for
  them.
- **Your edits live in the Schwung set**, on top of the kit that was loaded — so a set remembers
  which kit you used and what you changed. The kit files themselves are Move's, and are never
  modified.
- **Per-pad playback effects are not played back.** They're read and written back untouched, so kits
  stay lossless and reopen correctly on Move; DR32 just plays the plain sampler.
- **Synth pads are saved in the Schwung set, not in a kit file.** A Move kit is samples only, so
  loading one turns every pad back into a sample pad. A mixed kit lives in your set.
- A synth pad's velocity response is the engine's own, so its **Vel Vol** starts at 0. Turn it up to
  add DR32's velocity-to-volume on top.
- **Transpose on a drum-machine pad moves its Tune.** 9W9's kick is the exception: its Tune is the
  pitch sweep's time, not a pitch (the 909 kick's base note is fixed), so transpose leaves it alone.
- **Each drum-machine pad is a whole machine of its own.** Two 8W8 hat pads do not share the metal
  oscillators the way the machine's own hats do, and two CW-78 noise drums do not hear the same
  noise. A 9W9 pad holds about 380 KB of memory.
- **Return-chain effects saved inside a kit are preserved but not set up for you** — a kit's send
  *amounts* are imported, but what sits on the return is yours to choose.

## Credits and licence

**GPL-3.0-or-later** (MIT before the synth engines arrived). Not affiliated with Ableton.

DR32's synth drums are other people's instruments, carried over whole. Thank you to everyone who
made them and shared them:

| engine | by |
|---|---|
| **Simian** | [OneTrick SIMIAN 2](https://punklabs.com/ot-simian) by **Punk Labs** (GPL-3.0-or-later), unmodified. Its cymbal wavetable is a sample by **Kevin Hall** (CC0). |
| **Urchin** | OneTrick URCHIN by **Punk Labs** (GPL-3.0-or-later), unmodified. The OneTrick instruments are *free as in rights, not as in beer*: if you play them, [buy them from Punk Labs](https://punklabs.com). |
| **9W9** | [9W9](https://github.com/athousanddetails/schwung-9W9) by **athousanddetails** (GPL-3.0), which began as a port of [ER-99](https://github.com/matthewcieplak/er-99) by **Matthew Cieplak** (GPL-3.0); its hi-hat, ride and crash samples are ER-99's. |
| **6W6** | [6W6](https://github.com/athousanddetails/schwung-6W6) by **athousanddetails** (GPL-3.0), whose voices are [606-Inspired-Synth-Drums](https://github.com/analogcode/606-Inspired-Synth-Drums) by **Matthew Fecher / AudioKit Pro** (MIT). |
| **8W8** | [8W8](https://github.com/athousanddetails/schwung-8W8) by **athousanddetails** (GPL-3.0). Its rim shot follows sc808 from [Sonic Pi](https://github.com/sonic-pi-net/sonic-pi) (**Samuel Aaron and contributors**, MIT), adapted from **Yoshinosuke Horiuchi**'s SC-808. |
| **CW-78** | [CW-78](https://github.com/athousanddetails/schwung-cw-78) by **athousanddetails** (GPL-3.0). |
| **ChowKick** | [ChowKick](https://github.com/Chowdhury-DSP/ChowKick) by **Jatin Chowdhury / Chowdhury DSP** (BSD-3-Clause), ported to DR32, with Chowdhury DSP's chowdsp_wdf (BSD-3-Clause) running its diode circuit. |
| **FM** | DR32's own, after the idea of the Elektron Machinedrum's EFM machines. Its filter follows **Andy Simper**'s published state-variable filter. |
| **Sampler** | DR32's own reconstruction of Move's Drum Sampler voice. |

DR32 runs on [Schwung](https://github.com/charlesvestal/schwung) by **Charles Vestal**, who also
contributed fixes to DR32 itself. The Faust engines are compiled with [Faust](https://faust.grame.fr)
by GRAME. [`NOTICES.md`](NOTICES.md) lists every component with its licence and the exact version
DR32 carries.

The sample engine is a **reconstruction, not a design** — its behaviour comes from analysis of and
measurement against Move's own, which means any audible deviation is a bug even when it sounds
nicer.

---

## For developers

**Making another module's voices playable in DR32:** a module ships one extra file,
`dr32_engine.so`, and its sounds appear in DR32's engine picker. The spec and a walkthrough are in
[`docs/ENGINE_PLUGINS.md`](docs/ENGINE_PLUGINS.md); the contract is the one header
[`dsp/dr32_engine_api.h`](dsp/dr32_engine_api.h). There is a starter plugin in
[`docs/plugin_template/`](docs/plugin_template/) and a checker to run on your own machine,
[`tools/plugin_check.c`](tools/plugin_check.c).

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
| `dsp/` | the DSP: voice, kit loader, `.ablpreset` parsing, state, Resample; `dsp/engines/` the synth engines |
| `src/` | `module.json` (the served hierarchy), `engine_ui.json` (the synth engines' pages, generated), `help.json`, the browser and Resample canvases, the PAD cell widget |
| `lib/` | `.ablpreset` reading/writing |
| `tests/` · `tools/` | the off-device suite and the validators |
| `docs/` | the [manual](https://legsmechanical.github.io/schwung-dr32/manual.html), [`ENGINE_PLUGINS.md`](docs/ENGINE_PLUGINS.md) and design notes |

The conventions, and the traps worth knowing before changing anything, are in
[`CLAUDE.md`](CLAUDE.md).
