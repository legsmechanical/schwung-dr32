# Notices

## Licence

**GPL-3.0-or-later**, see [`LICENSE`](LICENSE). This applies from the release that added the synth
engines. DR32 was MIT until then. The synth engines are GPL-3.0-or-later, and a `dsp.so` that links
them is a combined work under the GPL. Earlier releases stay MIT.

## What is in the built module

`dist/dr32/dsp.so` is compiled from:

| | |
|---|---|
| **DR32**: `dsp/*.c`, `src/`, `tools/`, `tests/` | Copyright (c) 2026 **Josh Gaines / legsmechanical**, GPL-3.0-or-later. Earlier versions of this code were released under MIT, and **Charles Vestal**'s contributions (two audio-callback fixes, PRs #2 and #3) were made under that licence. The MIT notice below covers them. |
| **OneTrick SIMIAN 2 DSP**: `dsp/engines/simian/faust/` | Copyright (c) 2024 **Punk Labs LLC**, GPL-3.0-or-later. Unmodified, vendored from [schwung-simian](https://github.com/legsmechanical/schwung-simian). |
| **OneTrick URCHIN DSP**: `dsp/engines/urchin/faust/` | Copyright (c) 2023 **Punk Labs LLC**, GPL-3.0-or-later. Unmodified, vendored from schwung-urchin. |
| **The engines' factory values**: `dsp/engines/*/factory_bank.h` | Punk Labs' factory kits, converted by the ports' own tools. The synth models are rows of the "Basic" (SIMIAN) and "Init" (URCHIN) kits. |
| **Faust standard library** code, inlined into `dsp/engines/*/generated/*.hpp` by the Faust compiler | GRAME. It is emitted into the generated C++, and the result is distributed under this project's GPL-3.0-or-later. |
| **9W9** (TR-909 style): `dsp/engines/9w9/` | By **athousanddetails**, GPL-3.0. Unmodified, vendored from [schwung-9W9](https://github.com/athousanddetails/schwung-9W9) at `10cbe5c`. It started as a port of **[ER-99](https://github.com/matthewcieplak/er-99)** by **Matthew Cieplak** (GPL-3.0). |
| **9W9's hi-hat, ride and crash samples**: `src/samples/9w9/*.wav` | From ER-99 by Matthew Cieplak, shipped by 9W9. GPL-3.0. |
| **6W6** (TR-606 style): `dsp/engines/6w6/` | By **athousanddetails**, GPL-3.0. Unmodified, vendored from [schwung-6W6](https://github.com/athousanddetails/schwung-6W6) at `2bda07e`. |
| **606-Inspired-Synth-Drums**: `dsp/engines/6w6/*.hpp` | Copyright (c) 2026 **Matthew Fecher / AudioKit Pro**, **MIT**. [Upstream](https://github.com/analogcode/606-Inspired-Synth-Drums). The licence text is `dsp/engines/6w6/LICENSE.606.MIT`, and the notice is reproduced below. These are 6W6's drum voices, unmodified. |
| **8W8** (TR-808 style): `dsp/engines/8w8/` | By **athousanddetails**, GPL-3.0. Unmodified, vendored from [schwung-8W8](https://github.com/athousanddetails/schwung-8W8) at `94aa271`. Its rim shot is a transcription of **sc808** from [Sonic Pi](https://github.com/sonic-pi-net/sonic-pi)'s synth designs, which are **MIT** (Samuel Aaron and contributors; notice below). Sonic Pi adapted sc808 from **Yoshinosuke Horiuchi**'s SC-808, which he released free of charge for free use with no formal licence text. `sc_ugens.h` reimplements SuperCollider UGen behaviour; it copies no code (8W8's THIRD_PARTY.md). |
| **CW-78** (CR-78 style): `dsp/engines/cw78/` | By **athousanddetails**, GPL-3.0. Unmodified, vendored from [schwung-cw-78](https://github.com/athousanddetails/schwung-cw-78) at `17681a0`. Modelled from the Roland CR-78 service notes, which are not included. |
| **The engine adapters**: `dsp/engines/*.cpp`, `dsp/engines/*.c`, `kit_port.h`, `faust_voice.h`, `faust_shim.h` | Copyright (c) 2026 Josh Gaines / legsmechanical, GPL-3.0-or-later. |

SIMIAN's cymbal wavetable (`dsp/engines/simian/faust/samples/`) is a sample by **Kevin Hall**,
**CC0 1.0**. That directory's `LICENSE.txt` has the details.

The OneTrick instruments are *free as in rights, not as in beer*. If you play these engines,
[buy SIMIAN](https://punklabs.com/ot-simian) and URCHIN from Punk Labs.

## MIT notice for the earlier DR32 code

```
Copyright (c) 2026 Josh Gaines / legsmechanical, and Charles Vestal

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## MIT notice for the 606 voices (6W6)

```
MIT License

Copyright (c) 2026 Matthew Fecher

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## MIT notice for Sonic Pi's synth designs (8W8's sc808 rim shot)

Sonic Pi's `etc/synthdefs/` are MIT except for five named GPL-3.0 files, and sc808 is not one of
them. Sonic Pi's `LICENSE.md` says of it: *"The sc808 drum synths are adapted from Yoshinosuke
Horiuchi's SC-808, released free of charge with the author's published permission for free use; the
original carries no formal licence text. The adaptations retain attribution in their source
headers."* Retrieved from `sonic-pi-net/sonic-pi` `main`, 2026-09-22.

```
The MIT License (MIT)

Copyright (c) 2012 - 2026 Samuel Aaron and contributors (sam@sonic-pi.net)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```
