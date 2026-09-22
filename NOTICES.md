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
| **The engine adapters**: `dsp/engines/*.cpp`, `faust_voice.h`, `faust_shim.h` | Copyright (c) 2026 Josh Gaines / legsmechanical, GPL-3.0-or-later. |

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
