/* ckref — render ChowKick's DSP for one preset and one hit, as raw float32.
 *
 *   ckref <seconds> <velocity 1..127> <param=value>...   > out.f32
 *
 * Values are the plugin's own (percents 0..1). Mirrors ChowKick::processSynth
 * (src/ChowKick.cpp) minus the scope: trigger -> pulse shaper -> resonant
 * filter -> noise -> sum the four SIMD lanes -> output filter -> DC blocker,
 * 128-frame blocks, the note at frame 0 of block 0. prepareToPlay runs AFTER
 * the preset is applied, as when a host loads a saved plugin.
 */
#include "pch.h"
#include "dsp/Noise.h"
#include "dsp/OutputFilter.h"
#include "dsp/PulseShaper.h"
#include "dsp/ResonantFilter.h"
#include "dsp/Trigger.h"
#include <cstdio>

struct Host : juce::AudioProcessor {
    Host() : juce::AudioProcessor (BusesProperties().withOutput ("Out", juce::AudioChannelSet::mono())) {}
    const juce::String getName() const override { return "ckref"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
};

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 3) { std::fprintf (stderr, "usage: ckref <seconds> <vel> [id=value]...\n"); return 2; }
    const double fs = 44100.0;
    const int block = 128;
    const int total = (int) (std::atof (argv[1]) * fs);
    const int vel = std::atoi (argv[2]);

    Host host;
    Parameters params;
    Trigger::addParameters (params);
    Noise::addParameters (params);
    PulseShaper::addParameters (params);
    ResonantFilter::addParameters (params);
    OutputFilter::addParameters (params);
    juce::AudioProcessorValueTreeState vts (host, nullptr, "Parameters", { params.begin(), params.end() });

    for (int i = 3; i < argc; ++i)
    {
        juce::String kv (argv[i]);
        auto id = kv.upToFirstOccurrenceOf ("=", false, false);
        auto val = kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
        auto* p = vts.getParameter (id);
        if (p == nullptr) { std::fprintf (stderr, "no param %s\n", id.toRawUTF8()); return 2; }
        p->setValueNotifyingHost (p->convertTo0to1 (val));
        if (std::getenv ("CKREF_DEBUG"))
            std::fprintf (stderr, "%s = %g (asked %g)\n", id.toRawUTF8(),
                          p->convertFrom0to1 (p->getValue()), val);
    }

    Trigger trigger (vts, true, false);
    Noise noise (vts);
    ResonantFilter resFilter (vts, trigger);
    OutputFilter outFilter (vts);
    chowdsp::StateVariableFilter<float, chowdsp::StateVariableFilterType::Highpass> dcBlocker;

    trigger.resetTuning();
    trigger.prepareToPlay (fs, block);
    noise.prepareToPlay (fs, block);
    std::optional<PulseShaper> pulseShaper;
    pulseShaper.emplace (vts, fs);
    resFilter.reset (fs);
    outFilter.reset (fs);
    dcBlocker.prepare ({ fs, (juce::uint32) block, 1 });
    dcBlocker.setCutoffFrequency (10.0f);

    juce::HeapBlock<char> fourData;
    chowdsp::AudioBlock<Vec> four (fourData, 1, (size_t) block);
    juce::AudioBuffer<float> mono (1, block);

    for (int at = 0; at < total; at += block)
    {
        const int n = juce::jmin (block, total - at);
        juce::MidiBuffer midi;
        if (at == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) vel), 0);
        mono.clear();
        four.clear();
        trigger.processBlock (four, n, midi);
        pulseShaper->processBlock (four, n);
        resFilter.processBlock (four, n);
        noise.processBlock (four, n);
        auto* x = four.getChannelPointer (0);
        auto* y = mono.getWritePointer (0);
        for (int i = 0; i < n; ++i)
            y[i] = xsimd::reduce_add (x[i]);
        outFilter.processBlock (y, n);
        juce::dsp::AudioBlock<float> mb (mono.getArrayOfWritePointers(), 1, (size_t) n);
        juce::dsp::ProcessContextReplacing<float> ctx (mb);
        dcBlocker.process<juce::dsp::ProcessContextReplacing<float>> (ctx);
        std::fwrite (y, sizeof (float), (size_t) n, stdout);
    }
    return 0;
}
