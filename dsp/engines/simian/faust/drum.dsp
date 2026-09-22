/*
   ┏━━━┓╱╱╱╱╱┏┓╱╱┏┓╱╱╱╱╱┏┓╱╱   
   ┃┏━┓┃╱╱╱╱╱┃┃╱╱┃┃╱╱╱╱╱┃┃╱╱╱  
   ┃┗━┛┣┓┏┳━━┫┃┏┓┃┃╱╱┏━━┫┗━┳━━┓
   ┃┏━━┫┃┃┃┏┓┃┗┛┃┃┃╱┏┫┏┓┃┏┓┃━━┫
   ┃┃╱╱┃┗┛┃┃┃┃┏┓┃┃┗━┛┃┏┓┃┗┛┣━━┃
   ┗┛╱╱┗━━┻┛┗┻┛┗┛┗━━━┻┛┗┻━━┻━━┛
    ━━━━━━━━━━━━━━━━━━━━━━━━━━ 

	Copyright (c) 2024 Punk Labs LLC

	This file is part of OneTrick SIMIAN

	OneTrick SIMIAN is free software: you can redistribute it and/or modify it
	under the terms of the GNU General Public License as published by the Free
	Software Foundation, either version 3 of the License, or (at your option)
	any later version.

	OneTrick SIMIAN is distributed in the hope that it will be useful, but WITHOUT
	ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
	FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
	more details.

	You should have received a copy of the GNU General Public License along with
	OneTrick SIMIAN.  If not, see <http://www.gnu.org/licenses/>.
*/

declare name "OneTrick SIMIAN";
declare author "Oren Kurtz";
declare copyright "Punk Labs LLC 2024";
declare version "2.00";
declare license "GPLv3-or-later";

import("shared.lib");
import("params.lib");

cymbals = environment { import("samples/428614__hallkev__cymbal-swells-kevinsticks--22khz.lib"); };

simianModule(osc, oscWaveform, gain, lowpassFreq, oscPitch, oscEnvBend, envDecay, envPunch, fadeToneNoise, fadeClick, gate, chokeGate, velocity, gainDynamics, oscEnvBendDynamics, lowpassQ, saturation, pan, lowpassEnvBend, lowpassEnvBendDynamics, sendReverb) = result with {
	gainVelocity = it.interpolate_linear(gainDynamics, 1, velocity);
	toneVelocity = it.interpolate_linear(velocity, 1-oscEnvBendDynamics, 1);
	filterVelocity = it.interpolate_linear(velocity, 1-lowpassEnvBendDynamics, 1);
	
	hold = ba.sAndH(gate > gate');
	
	EnvDecayDynamics = 0.70; // 70% decay dynamics... minimum 30% decay time
	heldEnvDecay = envDecay/1000*it.interpolate_linear(EnvDecayDynamics, 1, velocity) : hold;
	heldEnvPunch = envPunch : hold;
	
	// Envelopes
	envSlopeMid = it.interpolate_linear(heldEnvPunch, 0.5, 0.9);
    env = ar_running_slope(simianAttackSeconds, heldEnvDecay, gate, 1, envSlopeMid) * chokeEnv;
    gainEnv = ba.lin2LogGain(ar_running_slope(simianAttackSeconds, heldEnvDecay, gate, ba.log2LinGain(gainVelocity), envSlopeMid) * chokeEnv);
    toneEnv = ar_running_slope(simianAttackSeconds, heldEnvDecay, gate, toneVelocity, envSlopeMid) * chokeEnv;
    filterEnv = ar_running_slope(simianAttackSeconds, heldEnvDecay, gate, filterVelocity, envSlopeMid) * chokeEnv;

    chokeEnv = en.ar(0, simianChokeSeconds, chokeGate) * chokeGate + (1-chokeGate);
    clickGainEnv = ba.lin2LogGain(ar_running(simianAttackSeconds, simianClickSeconds, gate, ba.log2LinGain(gainVelocity)));
    
    punchBend = 0;//12;
    punchEnv = 0;//ar_running(1/1000, 19/1000, gate, velocity)^2;
    
    toneSweep = ba.semi2ratio(oscEnvBend * toneEnv + punchBend*punchEnv);
    //filterSweep = it.interpolate_linear(filterSweep, 1, toneSweep);
    filterSweep = ba.semi2ratio(lowpassEnvBend * filterEnv);
    
    // Can be reused by osc, if wanted
	noiseFilter = fi.resonlp(min(lowpassFreq*filterSweep, 19000), max(0.01, lowpassQ), 1);
    noise = simianNoise : noiseFilter;
    pitch = oscPitch * toneSweep;
    tone = osc(pitch, oscWaveform, noiseFilter);
    drum = it.interpolate_linear(fadeToneNoise, tone, noise);
    
    // Crossfade Click with Drum (keeps volume in check)
    preResult = it.interpolate_linear(fadeClick*clickGainEnv, drum, noise) : *(gainEnv) : applySaturation(saturation) : *(gain);
    // Add Click (better for noise-only voices)
    //preResult = drum+(noise*clickGainEnv*fadeClick) : *(gainEnv) : applySaturation(saturation) : *(gain);
   
    // Mono:
    //result = preResult;
    // Stereo:
    //result = preResult <: (_*max(0,1-pan), _*max(0,pan+1));
    // Stereo+Sidechain
    result = (preResult <: (_*min(1,1-pan), _*min(1,pan+1)), preResult * sendReverb);
    // Reverb Test:
    //result = it.interpolate_linear(pan * 0.5 + 0.5, preResult, (preResult : re.satrev : select2(0))) <: si.bus(2);
};

simianModuleUI = result with {
   	osc(freq, wave, noiseFilter) = it.interpolate_linear(wave,
   		os.polyblep_triangle(freq) : fi.lowpass(1, 338.6), // Softer filtered Triangle
   		sampleOsc(freq/220, cymbals) : noiseFilter // Sampled Cymbal sound
   		); 
	result = simianModule(osc, oscWaveform, gain, lowpassFreq, oscPitch, oscEnvBend, envDecay, envPunch, fadeToneNoise, fadeClick, gate, chokeGate, velocity, gainDynamics, oscEnvBendDynamics, lowpassQ, saturation, pan, lowpassEnvBend, lowpassEnvBendDynamics, sendReverb);
};

process = simianModuleUI;
