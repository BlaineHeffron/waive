#include "AudioAnalysisCache.h"
#include "AudioAnalysis.h"

namespace waive
{

std::optional<AudioAnalysisSummary> AudioAnalysisCache::get (const CacheKey& key)
{
    const juce::ScopedLock sl (lock);
    auto it = cache.find (key);
    if (it != cache.end())
        return it->second;
    return std::nullopt;
}

void AudioAnalysisCache::put (const CacheKey& key, const AudioAnalysisSummary& summary)
{
    const juce::ScopedLock sl (lock);
    cache[key] = summary;
}

void AudioAnalysisCache::clear()
{
    const juce::ScopedLock sl (lock);
    cache.clear();
}

} // namespace waive
