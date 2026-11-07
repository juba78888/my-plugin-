#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

static double toLufs (double meanSquare)
{
    return -0.691 + 10.0 * std::log10 (juce::jmax (meanSquare, 1.0e-12));
}

LoudnessProcessor::LoudnessProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

bool LoudnessProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void LoudnessProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    subLen = juce::jmax (1, (int) std::lround (sr * 0.1));

    // Filtre K-weighting (ITU-R BS.1770) : etage 1 = high shelf
    {
        const double f0 = 1681.974450955533;
        const double G  = 3.999843853973347;
        const double Q  = 0.7071752369554196;
        const double K  = std::tan (juce::MathConstants<double>::pi * f0 / sr);
        const double Vh = std::pow (10.0, G / 20.0);
        const double Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;

        for (auto& f : stage1)
        {
            f.b0 = (Vh + Vb * K / Q + K * K) / a0;
            f.b1 = 2.0 * (K * K - Vh) / a0;
            f.b2 = (Vh - Vb * K / Q + K * K) / a0;
            f.a1 = 2.0 * (K * K - 1.0) / a0;
            f.a2 = (1.0 - K / Q + K * K) / a0;
        }
    }

    // Etage 2 = filtre passe-haut
    {
        const double f0 = 38.13547087602444;
        const double Q  = 0.5003270373238773;
        const double K  = std::tan (juce::MathConstants<double>::pi * f0 / sr);
        const double a0 = 1.0 + K / Q + K * K;

        for (auto& f : stage2)
        {
            f.b0 = 1.0;
            f.b1 = -2.0;
            f.b2 = 1.0;
            f.a1 = 2.0 * (K * K - 1.0) / a0;
            f.a2 = (1.0 - K / Q + K * K) / a0;
        }
    }

    oversampler.initProcessing ((size_t) peakChunk);
    oversampler.reset();
    resetMeasurements();
}

void LoudnessProcessor::resetMeasurements()
{
    for (auto& f : stage1) f.reset();
    for (auto& f : stage2) f.reset();

    subCount = 0;
    subK = subR = 0.0;
    ringK.fill (0.0);
    ringR.fill (0.0);
    ringPos = 0;
    filled = 0;
    std::fill (hist.begin(), hist.end(), 0.0);
    truePeakLin = 0.0;

    momentaryLufs = -120.0f;
    shortTermLufs = -120.0f;
    integratedLufs = -120.0f;
    rmsDb = -120.0f;
    truePeakDb = -120.0f;
}

void LoudnessProcessor::finishSubBlock()
{
    const double k = subK / (double) subLen;
    const double r = subR / (double) subLen / 2.0; // moyenne sur 2 canaux
    subK = subR = 0.0;
    subCount = 0;

    ringK[(size_t) ringPos] = k;
    ringR[(size_t) ringPos] = r;
    ringPos = (ringPos + 1) % ringSize;
    filled = juce::jmin (filled + 1, ringSize);

    auto meanLast = [this] (const std::array<double, ringSize>& ring, int n)
    {
        const int count = juce::jmin (n, filled);
        double sum = 0.0;
        for (int j = 0; j < count; ++j)
            sum += ring[(size_t) ((ringPos - 1 - j + ringSize * 2) % ringSize)];
        return count > 0 ? sum / (double) count : 0.0;
    };

    const double momentaryMs = meanLast (ringK, 4);
    const double shortMs     = meanLast (ringK, 30);
    const double rmsMs       = meanLast (ringR, 3);

    momentaryLufs = (float) toLufs (momentaryMs);
    shortTermLufs = (float) toLufs (shortMs);
    rmsDb = (float) (10.0 * std::log10 (juce::jmax (rmsMs, 1.0e-12)));

    // Mesure integree : un bloc de 400 ms toutes les 100 ms
    if (filled >= 4)
    {
        const double l = toLufs (momentaryMs);
        if (l >= -70.0)
        {
            const int idx = juce::jlimit (0, histBins - 1, (int) std::floor ((l + 70.0) / 0.1));
            hist[(size_t) idx] += 1.0;
        }
        updateIntegrated();
    }
}

void LoudnessProcessor::updateIntegrated()
{
    auto binEnergy = [] (int i)
    {
        const double l = -70.0 + i * 0.1 + 0.05;
        return std::pow (10.0, (l + 0.691) / 10.0);
    };

    // Porte absolue (-70 LUFS) : tous les bins du histogramme
    double sum = 0.0, count = 0.0;
    for (int i = 0; i < histBins; ++i)
    {
        sum += hist[(size_t) i] * binEnergy (i);
        count += hist[(size_t) i];
    }
    if (count <= 0.0)
        return;

    // Porte relative : 10 LU sous la moyenne
    const double relThreshold = toLufs (sum / count) - 10.0;

    double sum2 = 0.0, count2 = 0.0;
    for (int i = 0; i < histBins; ++i)
    {
        const double l = -70.0 + i * 0.1 + 0.05;
        if (l > relThreshold)
        {
            sum2 += hist[(size_t) i] * binEnergy (i);
            count2 += hist[(size_t) i];
        }
    }
    if (count2 > 0.0)
        integratedLufs = (float) toLufs (sum2 / count2);
}

void LoudnessProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (resetRequested.exchange (false))
        resetMeasurements();

    const int n = buffer.getNumSamples();
    const int nCh = juce::jmin (2, buffer.getNumChannels());
    if (nCh < 2 || n == 0)
        return;

    // --- LUFS + RMS ---
    const float* in[2] = { buffer.getReadPointer (0), buffer.getReadPointer (1) };

    for (int i = 0; i < n; ++i)
    {
        double ks = 0.0, rs = 0.0;
        for (int ch = 0; ch < 2; ++ch)
        {
            const double x = (double) in[ch][i];
            rs += x * x;
            const double y = stage2[(size_t) ch].process (stage1[(size_t) ch].process (x));
            ks += y * y;
        }
        subK += ks;
        subR += rs;

        if (++subCount >= subLen)
            finishSubBlock();
    }

    // --- True Peak (suréchantillonnage x4) ---
    double peak = truePeakLin;
    for (int start = 0; start < n; start += peakChunk)
    {
        const int len = juce::jmin (peakChunk, n - start);
        juce::dsp::AudioBlock<const float> block (buffer.getArrayOfReadPointers(),
                                                  (size_t) 2, (size_t) start, (size_t) len);
        auto up = oversampler.processSamplesUp (block);

        for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
        {
            const float* p = up.getChannelPointer (ch);
            for (size_t s = 0; s < up.getNumSamples(); ++s)
                peak = juce::jmax (peak, (double) std::abs (p[s]));
        }
    }
    truePeakLin = peak;
    truePeakDb = (float) (20.0 * std::log10 (juce::jmax (peak, 1.0e-6)));

    // Le signal audio passe sans modification
}

juce::AudioProcessorEditor* LoudnessProcessor::createEditor()
{
    return new LoudnessEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LoudnessProcessor();
}
