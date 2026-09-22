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

declare name "OneTrick URCHIN DSP";
declare copyright "Copyright (c) 2023 Punk Labs LLC";
declare license "GPLv3 (or later)";

import("stdfaust.lib");
import("shared.lib");


closed = hslider("Closed", 0, 0, 1, 0.01);
open = 1-closed;

dampen = it.interpolate_linear(1 - (hslider("[010]Damp[export:Damp][unit:%]", 0, 0, 100, 0.0001) / 100), 0.1, 1.0);

decay = dampen * it.interpolate_linear(pow(size01, 3), 0.5, 4) * it.interpolate_linear(closed, 1, 0.05) / pitchShift;
//decay = hslider("/v:[01]Main/Decay", 5, 0.01, 10, 0.01);

cutoffMix = 1;//hslider("/v:[02]Mix/[01]Cutoff Filters", 1, 0, 1, 0.01);
notchMix = 1;//hslider("/v:[02]Mix/[02]Notch Filters", 1.0, 0, 1, 0.01);

sizeIn = hslider("[020]Cymbal_Size[export:Size][unit:in]", 14, 10, 24, 0.01);
size01 = sizeIn : it.remap(10, 24, 0, 1);
crash01 = hslider("[030]Cymbal_Crash[export:Crash][unit:%]", 50, 0, 100, 0.01) / 100;


setPartialFreq = 50;
setPartialLevel = 1;
set0 = 1;
setA = 0.999;
setOddHarmonics = 0;
partialSynth(i0) = result with {
    i=i0+1;
    partialLevel = setPartialLevel;//hslider("/v:[1%i]Partial %i/[020]Level %i", setPartialLevel, 0, 1, 0.01);
    partialFreq = setPartialFreq;//hslider("/v:[1%i]Partial %i/[030]Freq %i", setPartialFreq, 50, 600, 0.01) * (0.5+oddHarmonics/2) * pitchShift;
    partial0 = 0.999;//hslider("/v:[1%i]Partial %i/[040]b0 %i", set0, -1, 1, 0.01);
    partialA = setA;//hslider("/v:[1%i]Partial %i/[050]bA/M %i", setA, 0, 0.9999, 0.01) * (-1+oddHarmonics*2);
    oddHarmonics = setOddHarmonics;//hslider("/v:[1%i]Partial %i/[060]Odd Harmonics", setOddHarmonics, 0, 1, 1);
    maxDelay = ma.SR/1;
    modulatedFreq = partialFreq*(os.osc(15.31)*0.006+1)*(os.osc(9.31)*0.006+1);
    delay = ma.SR/modulatedFreq;

    //noise = ot.noise*strikeEnv;
    noise = ot.noises(4, i0)*strikeEnv:customSaturation(pluginVelocity); //Crunch up harder hits
    //noise = no.velvet_noise(1, 1000)*4;
    // Can't use fcomb or it dulls the sound
    dampHack = 0.05;//+i0/20; //Different decay rates?

    t60 = decay;
    decayRatio = pow(ba.tau2pole(t60), delay);

    comb = fi.fb_fcomb(maxDelay, int(delay)+dampHack, partial0, partialA*decayRatio);
    result = (noise:comb)*0.2*partialLevel;
};

//808: 205.3, 369.6, 304.4, 522.7, 800 (359.4–1149.9), and 540 (254.3–627.2) Hz
synth = 0
: +(partialSynth[
    setPartialLevel = 1.0;
    setPartialFreq = 120.54;//127.45;//205.3;//66.64;
    setA=0.9999;
    setOddHarmonics=0;
](0))
: +(partialSynth[
    setPartialLevel = 1.0;
    setPartialFreq = 163.38;//171.67;//369.6;//88.75;
    setA=0.9999; // Negative values shift the harmonics 50% to the right (i think)
    setOddHarmonics=0;
](1))
: +(partialSynth[
    setPartialLevel = 0.8;
    setPartialFreq = 189.64;//304.4;//117.77;
    setA=0.9999;
    setOddHarmonics=0;
](2))
: +(partialSynth[
    setPartialLevel = 0.6;
    setPartialFreq = 85;//152.31;//522.7;
    setA=0.9999;
    setOddHarmonics=0;
](3)) /*
: +(partialSynth[
    setPartialLevel = 0.0;
    setPartialFreq = 100;
    setA=0.9999;
    setOddHarmonics=1;
](4))
: +(partialSynth[
    setPartialLevel = 0.0;
    setPartialFreq = 100;
    setA=0.9999;
](5)) */
*(ba.db2linear(-9));

interpolate_freq(t, from, to) = result with {
    result = ba.midikey2hz(it.interpolate_linear(t, ba.hz2midikey(from), ba.hz2midikey(to)));
};

env = energyTracker(decay, pluginTrigger, pluginVelocity);

strikeEnv = energyTracker(it.interpolate_linear(size01, 0.200, 0.240), pluginTrigger, pluginVelocity);

lowEnv = env; //energyTracker(decay, pluginTrigger, pluginVelocity);
lowCutAmount = size01 * it.interpolate_linear(open, 0.5, 1.0);
lowCutRest = interpolate_freq(lowCutAmount, 4800, 1000);
lowCutExcite = interpolate_freq(lowCutAmount, 4800, 150);
lowCut = interpolate_freq(lowEnv, lowCutRest, lowCutExcite);

// highEnv = env; //energyTracker(decay, pluginTrigger, pluginVelocity);
// highCut = interpolate_freq(highEnv, MAX_FREQ, 4800);

//highEnv = pow(en.ar(60*size01/1000, 800/1000, pluginTrigger), 3); //Starts super fast, but then slows down dramatically...
highEnv = energyTracker(decay*0.5, pluginTrigger, pluginVelocity);
highCutRest = 17000;
highCutExcite = 400;//interpolate_freq(size01, 2000, 500);
highCut = interpolate_freq(highEnv, highCutRest, highCutExcite);

//crashFilter = ef.dryWetMixer(crash01, fi.lowpass(2, highCut*pitchShift:clampFreq));
crashFilter = ef.dryWetMixer(crash01, fi.peak_eq(5, f-f*0.5:clampFreq, f+f*0.5:clampFreq)) with {
    f = highCut*pitchShift;
};

cutoffFilters = ef.dryWetMixer(cutoffMix, _
    : fi.highpass(2, lowCut*pitchShift:clampFreq)

    // Removing lowest freqs for a better mix
    : fi.highpass(4, 300*pitchShift)
    : fi.highpass(2, 500*pitchShift)

    : fi.lowpass(2, interpolate_freq(size01, 8000, 12800)*pitchShift:min(MAX_FREQ))
);

//shifted_notchw(a,b) = fi.notchw(a*pitchShift, b*pitchShift);
//shifted_peak_eq(a,b,c) = fi.peak_eq(a*pitchShift, b*pitchShift, c*pitchShift);

notchFilters = ef.dryWetMixer(notchMix, _
    : fi.notchw(50*pitchShift, 415*pitchShift) // Kill bad harmonic
    //: fi.notchw(100*pitchShift, 580*pitchShift) // Cut band observed
    //: fi.peak_eq(6, 600*pitchShift, 450*pitchShift)
    //: fi.notchw(100*pitchShift, 850*pitchShift) // Cut band observed
    //: fi.peak_eq(6, 1200*pitchShift, 900*pitchShift)
    : fi.notchw(50*pitchShift, 1230*pitchShift) // Kill bad harmonic
    : fi.peak_eq(-6, 1300*pitchShift, 500*pitchShift) // Cut band observed
    : fi.peak_eq(-6, 2000*pitchShift, 500*pitchShift) // Cut band observed
    : fi.notchw(400*pitchShift, 4600*pitchShift) // Cut band observed
    /*
    : fi.notchw(800*pitchShift, 2733*pitchShift) // Cut band observed
    : fi.notchw(800*pitchShift, 3724*pitchShift) // Cut band observed
    : fi.notchw(800*pitchShift, 7960*pitchShift) // Cut band observed
    */
);


strike = strikeClick * mixStrike * ba.db2linear(6);
// Some extra noise to augment the normal drum strike click
strikeImpactNoise = ot.noise*strikeEnv*mixStrike
    : fi.highpass(2, 2000*pitchShift)
    //: fi.peak_eq(16, 1000*pitchShift, 500*pitchShift) // Bell
    : fi.lowpass(1, interpolate_freq(strikeEnv, interpolate_freq(strikeBrightness, 2000, 17000), MAX_FREQ)*pitchShift:min(MAX_FREQ)) : *(0.5);

// Compensate for weaker small cymbals
globalAdjust = ba.db2linear(it.interpolate_linear(size01, 3, -3));

// Keep the strike level the same despite size changes (hack)
strikeAdjust = (1/globalAdjust) * ba.db2linear(-2);

processMono = synth
    : crashFilter
    : +(strikeImpactNoise*strikeAdjust) // Apply after crashFilter!
    : cutoffFilters
    : notchFilters
    : +(strike*(strikeAdjust));

// Add some stereo depth, not sure if this is a great solution...
// addStereoDepth = (_,(_:de.delay(d, d))) with {
//     d = 1/1000*ma.SR;
// };
 addStereoDepth = si.bus(2); // Rely on reverb alone?

process = (voice <: si.bus(2) : addStereoDepth : stereoPanner(voicePan), voice*voiceReverbSend) with {
    voice = processMono : *(ba.db2linear(-19) * voiceGain * globalAdjust) 
    : voiceChain;
};

//process = processMono <: si.bus(2);
