/*
   ┏━━━┓╱╱╱╱╱┏┓╱╱┏┓╱╱╱╱╱┏┓╱╱   
   ┃┏━┓┃╱╱╱╱╱┃┃╱╱┃┃╱╱╱╱╱┃┃╱╱╱  
   ┃┗━┛┣┓┏┳━━┫┃┏┓┃┃╱╱┏━━┫┗━┳━━┓
   ┃┏━━┫┃┃┃┏┓┃┗┛┃┃┃╱┏┫┏┓┃┏┓┃━━┫
   ┃┃╱╱┃┗┛┃┃┃┃┏┓┃┃┗━┛┃┏┓┃┗┛┣━━┃
   ┗┛╱╱┗━━┻┛┗┻┛┗┛┗━━━┻┛┗┻━━┻━━┛
    ━━━━━━━━━━━━━━━━━━━━━━━━━━ 

	Copyright (c) 2023 Punk Labs LLC

	This file is part of OneTrick URCHIN

	OneTrick URCHIN is free software: you can redistribute it and/or modify it
	under the terms of the GNU General Public License as published by the Free
	Software Foundation, either version 3 of the License, or (at your option)
	any later version.

	OneTrick URCHIN is distributed in the hope that it will be useful, but WITHOUT
	ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
	FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
	more details.

	You should have received a copy of the GNU General Public License along with
	OneTrick URCHIN.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
   media.dsp — URCHIN's MEDIA and SAMPLER stages, per pad, for DR32.

   NOT part of upstream: assembled for DR32 (schwung-dr32) from the global
   output stage of OneTrick URCHIN (output.dsp), whose definitions below are
   copied VERBATIM. In URCHIN these run once, over the whole kit; DR32 runs
   one per URCHIN pad, after that pad's voice, so a kit of mixed engines can
   still give its URCHIN drums the record they are pressed onto.

   Kept, in URCHIN's own order: the record/tape noise (which follows the
   drum's level), the sample-rate reducer and bit crusher, the saturation.
   Left out: the reverb (DR32's sends are the reverb), Low Cut / High Cut
   (Josh, 2026-09-21: "let's drop the low/high cut"), the limiter and the
   volume (DR32's Volume).

   MONO: DR32 pans every pad itself, so only the left channel is taken; Faust
   prunes the right one entirely.
*/

declare name "OneTrick URCHIN media stage (DR32 per-pad)";
declare copyright "Copyright (c) 2023 Punk Labs LLC";
declare license "GPLv3 (or later)";

import("stdfaust.lib");
import("shared.lib");

globalNoiseAmount = hslider("[100]Global_Noise[export:Noise Amount][unit:%]", 0, 0, 100, 0.01)/100;
globalNoiseType = hslider("[110]Global_NoiseType[export:Noise Type][enum:Vinyl,Tape]", 0, 0, 1, 1);
globalSaturation = hslider("[120]Global_Saturation[export:Saturation][unit:%]", 0, 0, 100, 0.01) * 0.01;
globalSamplerate = hslider("[140]Global_SampleRate[export:Samplerate][unit:kHz]", 44.1, 11.025, 44.1, 0.01) * 1000 : min(ma.SR);
globalBitdepth = hslider("[150]Global_Bits[export:Bits][unit:bit]", 16, 4, 16, 1);

stereoTapeNoise = ((ot.noises(2, 0):filter), (ot.noises(2, 1):filter)) : stereo(*(ba.db2linear(-24))) with {
    filter = fi.lowshelf(1, 17, 100) : fi.lowpass(24, 16500 : min(MAX_FREQ));
};
stereoVinylNoise = ((noiseChannel(0)), (noiseChannel(1))) with {
    filter = fi.lowshelf(1, 17, 100) : ef.dryWetMixer(0.75,fi.lowpass(1, 500)) : fi.peak_eq(warble*10, 1500, 3500) : fi.lowpass(24, 8000);
    rpmFreq = 78/60;
    wobble1 = os.osc(rpmFreq) : it.remap(-1, 1, 0, 1);
    wobble2 = os.osc(rpmFreq*2) : it.remap(-1, 1, 0, 1);
    wobble4 = os.osc(rpmFreq*4) : it.remap(-1, 1, 0, 1);
    warble01 = it.interpolate_linear(wobble1*wobble2*wobble4, 0, 1);
    warble = it.interpolate_linear(warble01, 0.8, 1);
    noiseChannel(x) = ot.noises(4, x) + pop + crackle : *(warble) : *(ba.db2linear(-21)) : filter with {
        attackDecay = ot.noise : it.remap(-1, 1, 1, 8) : ba.downSample(10) : /(1000);
        popFilter = fi.bandpass(1, 500, 4000);
        popFrequency = 1.5;
        pop = ot.noises(4, x+2) * en.ar(attackDecay, attackDecay, ot.velvet_noise(1, popFrequency)) : popFilter : *(15);
        crackleFrequency = 10*warble;
        crackle = ot.noises(4, x+2) * en.ar(attackDecay*0.25, attackDecay*0.25, ot.velvet_noise(1, crackleFrequency)) : popFilter : *(10);
    };
};



// The filter should come before downsampling, i think, but it sounds so much better after.
addDownsampling = ba.downSample(globalSamplerate) : hardware_filter with {
    hardware_filter = fi.lowpass(8, min(MAX_FREQ, ot.NYQUIST_AT(globalSamplerate)));
};

addBitcrusher = _
	: ef.dryWetMixer(globalSamplerate < 44099.5, addDownsampling)
	: ef.dryWetMixer(globalBitdepth < 15.5, ba.bitcrusher(globalBitdepth));



addGlobalSaturation = customSaturation(globalSaturation);

interleaveDualStereo = route(4,4,(1,1),(3,2),(2,3),(4,4));
globalNoise = (stereoVinylNoise, stereoTapeNoise) : interleaveDualStereo :> par(i, 2, it.interpolate_linear(globalNoiseType)) : stereo(*(globalNoiseAmount));
//globalNoise = stereoTapeNoise;
addGlobalNoise(l, r) = (l, r) <: (si.bus(2), (globalNoise : stereo(*(noiseLevel)))) : interleaveDualStereo :> stereo(+) with {
	noiseGateLevel = ba.db2linear(-80);
	noiseFloor = ba.db2linear(-24);
	noiseLevel = (l+r)/2 : an.amp_follower_ud(0.1/1000, 50/1000) : /(noiseGateLevel) : min(1) : *(noiseFloor); //track in mono
};


process(x) = (x, x) : addGlobalNoise : stereo(addBitcrusher : addGlobalSaturation) : (_, !);
