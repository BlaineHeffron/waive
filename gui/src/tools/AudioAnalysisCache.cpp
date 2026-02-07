#include "AudioAnalysisCache.h"
#include "AudioAnalysis.h"

namespace waive
{

AudioAnalysisCache::AudioAnalysisCache (int maxEntries_)
    : maxEntries (maxEntries_)
{
}

std::optional<AudioAnalysisSummary> AudioAnalysisCache::get (const CacheKey& key)
{
    const juce::ScopedLock sl (lock);
    auto it = cache.find (key);
    if (it != cache.end())
    {
        // Move accessed key to front (most recently used)
        accessOrder.remove (key);
        accessOrder.push_front (key);
        return it->second;
    }
    return std::nullopt;
}

void AudioAnalysisCache::put (const CacheKey& key, const AudioAnalysisSummary& summary)
{
    const juce::ScopedLock sl (lock);

    // If already exists, update and move to front
    auto it = cache.find (key);
    if (it != cache.end())
    {
        it->second = summary;
        accessOrder.remove (key);
        accessOrder.push_front (key);
        return;
    }

    // Evict LRU if at capacity
    if ((int) cache.size() >= maxEntries && ! accessOrder.empty())
    {
        auto lru = accessOrder.back();
        accessOrder.pop_back();
        cache.erase (lru);
    }

    // Insert new entry
    cache[key] = summary;
    accessOrder.push_front (key);
}

void AudioAnalysisCache::clear()
{
    const juce::ScopedLock sl (lock);
    cache.clear();
    accessOrder.clear();
}

} // namespace waive
