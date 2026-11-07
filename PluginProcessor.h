#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() noexcept { z1 = z2 = 0; }
};

class LoudnessProcessor : public juce::AudioProcessor
{
public:
    LoudnessProcessor();
    ~LoudnessProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Loudness Meter"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    // Valeurs lues par l'interface (en dB / LUFS)
    std::atomic<float> momentaryLufs { -120.0f };
    std::atomic<float> shortTermLufs { -120.0f };
    std::atomic<float> integratedLufs { -120.0f };
    std::atomic<float> rmsDb { -120.0f };
    std::atomic<float> truePeakDb { -120.0f };

    void requestReset() { resetRequested = true; }

private:
    static constexpr int ringSize = 30;      // 30 x 100 ms = 3 s
    static constexpr int histBins = 801;     // -70 a +10 LUFS, pas de 0.1
    static constexpr int peakChunk = 512;

    void resetMeasurements();
    void finishSubBlock();
    void updateIntegrated();

    double sampleRate = 48000.0;
    int subLen = 4800;
    int subCount = 0;
    double subK = 0.0, subR = 0.0;

    std::array<Biquad, 2> stage1, stage2;

    std::array<double, ringSize> ringK {}, ringR {};
    int ringPos = 0, filled = 0;

    std::vector<double> hist = std::vector<double> (histBins, 0.0);

    double truePeakLin = 0.0;

    juce::dsp::Oversampling<float> oversampler { 2, 2,
        juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true };

    std::atomic<bool> resetRequested { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoudnessProcessor)
};
