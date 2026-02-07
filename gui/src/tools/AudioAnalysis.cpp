#include "AudioAnalysis.h"
#include "AudioAnalysisCache.h"

#include <algorithm>
#include <cmath>

namespace waive
{

AudioAnalysisSummary analyseAudioFile (const juce::File& sourceFile,
                                       float activityThresholdGain,
                                       float transientRiseThresholdGain,
                                       const std::function<bool()>& shouldCancel,
                                       AudioAnalysisCache* cache)
{
#ifdef WAIVE_PROFILE_TOOLS
    const auto startTime = juce::Time::getMillisecondCounterHiRes();
    const auto fileSize = sourceFile.getSize();
    DBG ("AudioAnalysis: Starting analysis of " << sourceFile.getFileName()
         << " (" << (fileSize / 1024) << " KB)");
#endif

    if (cache != nullptr)
    {
        AudioAnalysisCache::CacheKey key { sourceFile, activityThresholdGain, transientRiseThresholdGain };
        if (auto cached = cache->get (key))
        {
#ifdef WAIVE_PROFILE_TOOLS
            DBG ("AudioAnalysis: Cache HIT for " << sourceFile.getFileName());
#endif
            return *cached;
        }
#ifdef WAIVE_PROFILE_TOOLS
        else
        {
            DBG ("AudioAnalysis: Cache MISS for " << sourceFile.getFileName());
        }
#endif
    }

    AudioAnalysisSummary summary;

    if (! sourceFile.existsAsFile())
        return summary;

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (sourceFile));
    if (reader == nullptr || reader->numChannels <= 0 || reader->lengthInSamples <= 0)
        return summary;

    // Validate sample rate to prevent division by zero downstream
    if (reader->sampleRate <= 0)
        return summary;

    summary.valid = true;
    summary.sampleRate = reader->sampleRate;
    summary.totalSamples = reader->lengthInSamples;

    constexpr int blockSize = 8192;
    juce::AudioBuffer<float> buffer ((int) reader->numChannels, blockSize);

    const float threshold = juce::jmax (0.0f, activityThresholdGain);
    const float transientRise = juce::jmax (0.0f, transientRiseThresholdGain);

    int64 samplePos = 0;
    float envelope = 0.0f;

    while (samplePos < summary.totalSamples)
    {
        if (shouldCancel && shouldCancel())
        {
            summary.cancelled = true;
            summary.valid = false;
            return summary;
        }

        const auto samplesThisBlock = (int) std::min<int64> ((int64) blockSize,
                                                             summary.totalSamples - samplePos);
        if (samplesThisBlock <= 0)
            break;

        if (! reader->read (&buffer, 0, samplesThisBlock, samplePos, true, true))
            break;

        for (int s = 0; s < samplesThisBlock; ++s)
        {
            float samplePeak = 0.0f;

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                samplePeak = juce::jmax (samplePeak, std::abs (buffer.getSample (ch, s)));

            summary.peakGain = juce::jmax (summary.peakGain, samplePeak);

            const auto absoluteSample = samplePos + s;
            if (samplePeak >= threshold)
            {
                if (summary.firstAboveSample < 0)
                    summary.firstAboveSample = absoluteSample;

                summary.lastAboveSample = absoluteSample;
            }

            if (summary.firstTransientSample < 0
                && summary.firstAboveSample >= 0
                && samplePeak >= threshold
                && (samplePeak - envelope) >= transientRise)
            {
                summary.firstTransientSample = absoluteSample;
            }

            envelope += (samplePeak - envelope) * 0.01f;
        }

        samplePos += samplesThisBlock;
    }

    if (summary.firstTransientSample < 0)
        summary.firstTransientSample = summary.firstAboveSample;

    if (cache != nullptr && summary.valid)
    {
        AudioAnalysisCache::CacheKey key { sourceFile, activityThresholdGain, transientRiseThresholdGain };
        cache->put (key, summary);
    }

#ifdef WAIVE_PROFILE_TOOLS
    const auto elapsed = juce::Time::getMillisecondCounterHiRes() - startTime;
    DBG ("AudioAnalysis: Completed in " << juce::String (elapsed, 2) << " ms"
         << " (" << juce::String (summary.totalSamples / summary.sampleRate, 2) << " s duration)");
#endif

    return summary;
}

} // namespace waive
