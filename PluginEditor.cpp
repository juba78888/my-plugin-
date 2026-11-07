#include "PluginEditor.h"

static void drawMeter (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& label,
                       float value, const juce::String& unit, float lo, float hi, juce::Colour colour)
{
    g.setColour (juce::Colour (0xffb0b0b0));
    g.setFont (15.0f);
    g.drawText (label, r.getX(), r.getY(), 200, 22, juce::Justification::centredLeft);

    const juce::String text = value <= -100.0f ? juce::String ("-inf") + " " + unit
                                               : juce::String (value, 1) + " " + unit;
    g.setColour (juce::Colours::white);
    g.setFont (22.0f);
    g.drawText (text, r.getX(), r.getY(), r.getWidth(), 22, juce::Justification::centredRight);

    auto bar = juce::Rectangle<float> ((float) r.getX(), (float) r.getY() + 28.0f, (float) r.getWidth(), 10.0f);
    g.setColour (juce::Colour (0xff2a2d33));
    g.fillRoundedRectangle (bar, 3.0f);

    const float prop = juce::jlimit (0.0f, 1.0f, (value - lo) / (hi - lo));
    g.setColour (colour);
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * prop), 3.0f);
}

LoudnessEditor::LoudnessEditor (LoudnessProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    addAndMakeVisible (resetButton);
    resetButton.onClick = [this] { proc.requestReset(); };
    setSize (420, 340);
    startTimerHz (30);
}

void LoudnessEditor::resized()
{
    resetButton.setBounds (getWidth() - 90, 14, 70, 26);
}

void LoudnessEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff17191d));

    g.setColour (juce::Colours::white);
    g.setFont (20.0f);
    g.drawText ("Loudness Meter", 20, 14, 250, 26, juce::Justification::centredLeft);

    const float tp = proc.truePeakDb.load();
    const auto tpColour = tp > -1.0f ? juce::Colour (0xffe5484d)
                        : tp > -3.0f ? juce::Colour (0xfff5a524)
                                     : juce::Colour (0xff3dd68c);

    const auto lufsColour = juce::Colour (0xff2bb3c0);
    const auto rmsColour = juce::Colour (0xff5b8def);

    int y = 60;
    const int step = 54;
    const int w = getWidth() - 40;

    drawMeter (g, { 20, y, w, 44 }, "RMS (300 ms)", proc.rmsDb.load(), "dBFS", -60.0f, 0.0f, rmsColour);
    y += step;
    drawMeter (g, { 20, y, w, 44 }, "True Peak (max)", tp, "dBTP", -60.0f, 0.0f, tpColour);
    y += step;
    drawMeter (g, { 20, y, w, 44 }, "LUFS Momentary", proc.momentaryLufs.load(), "LUFS", -60.0f, 0.0f, lufsColour);
    y += step;
    drawMeter (g, { 20, y, w, 44 }, "LUFS Short-term", proc.shortTermLufs.load(), "LUFS", -60.0f, 0.0f, lufsColour);
    y += step;
    drawMeter (g, { 20, y, w, 44 }, "LUFS Integrated", proc.integratedLufs.load(), "LUFS", -60.0f, 0.0f, lufsColour);
}
