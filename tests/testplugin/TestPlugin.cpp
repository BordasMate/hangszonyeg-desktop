#include <juce_audio_utils/juce_audio_utils.h>
// Próbaplugin: a bal csatornát felére halkítja, a jobbat ellenfázisba fordítja; így a körút pontosan ellenőrizhető.
class HzTest final : public juce::AudioProcessor
{
public:
    HzTest() : AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo()).withOutput ("Out", juce::AudioChannelSet::stereo())) {}
    const juce::String getName() const override { return "HZ Teszt"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        b.applyGain (0, 0, b.getNumSamples(), 0.5f);
        if (b.getNumChannels() > 1) b.applyGain (1, 0, b.getNumSamples(), -1.0f);
    }
    bool isBusesLayoutSupported (const BusesLayout& l) const override { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() && l.getMainInputChannelSet() == l.getMainOutputChannelSet(); }
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor (*this); }
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& m) override { m.append ("hz-state-ok", 11); }
    void setStateInformation (const void* d, int n) override { lastState = juce::String::fromUTF8 ((const char*) d, n); }
    juce::String lastState;
};
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new HzTest(); }
