#pragma once
#include "PluginProcessor.h"

class LoudnessEditor : public juce::AudioProcessorEditor,
                       private juce::Timer
{
public:
    explicit LoudnessEditor (LoudnessProcessor&);
    ~LoudnessEditor() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { repaint(); }

    LoudnessProcessor& proc;
    juce::TextButton resetButton { "Reset" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoudnessEditor)
};
