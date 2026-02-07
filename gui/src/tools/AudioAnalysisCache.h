#pragma once

#include <JuceHeader.h>
#include <unordered_map>
#include <list>
#include "AudioAnalysis.h"

namespace waive
{

/** Cache for audio analysis results to avoid redundant file I/O and processing. */
class AudioAnalysisCache
{
public:
    struct CacheKey
    {
        juce::File sourceFile;
        float activityThreshold;
        float transientThreshold;

        bool operator== (const CacheKey& other) const
        {
            return sourceFile == other.sourceFile &&
                   std::abs (activityThreshold - other.activityThreshold) < 0.0001f &&
                   std::abs (transientThreshold - other.transientThreshold) < 0.0001f;
        }
    };

    struct CacheKeyHash
    {
        std::size_t operator() (const CacheKey& key) const
        {
            auto h1 = std::hash<juce::String>() (key.sourceFile.getFullPathName());
            auto h2 = std::hash<float>() (key.activityThreshold);
            auto h3 = std::hash<float>() (key.transientThreshold);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };

    explicit AudioAnalysisCache (int maxEntries = 256);
    ~AudioAnalysisCache() = default;

    std::optional<AudioAnalysisSummary> get (const CacheKey& key);
    void put (const CacheKey& key, const AudioAnalysisSummary& summary);
    void clear();

private:
    std::unordered_map<CacheKey, AudioAnalysisSummary, CacheKeyHash> cache;
    std::list<CacheKey> accessOrder;
    int maxEntries;
    juce::CriticalSection lock;
};

} // namespace waive
