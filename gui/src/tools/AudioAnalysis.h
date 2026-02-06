#pragma once

#include <JuceHeader.h>
#include <functional>

namespace waive
{

struct AudioAnalysisSummary
{
    bool valid = false;
    bool cancelled = false;
    double sampleRate = 0.0;
    int64 totalSamples = 0;
    float peakGain = 0.0f;
    int64 firstAboveSample = -1;
    int64 lastAboveSample = -1;
    int64 firstTransientSample = -1;
};

AudioAnalysisSummary analyseAudioFile (const juce::File& sourceFile,
                                       float activityThresholdGain,
                                       float transientRiseThresholdGain,
                                       const std::function<bool()>& shouldCancel = {});

} // namespace waive
