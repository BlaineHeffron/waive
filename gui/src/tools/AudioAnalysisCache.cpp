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
        // Move accessed key to front (most recently used) - O(1) with iterator map
        auto iterIt = iterMap.find (key);
        if (iterIt != iterMap.end())
            accessOrder.erase (iterIt->second);

        accessOrder.push_front (key);
        iterMap[key] = accessOrder.begin();
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

        // O(1) removal with iterator map
        auto iterIt = iterMap.find (key);
        if (iterIt != iterMap.end())
            accessOrder.erase (iterIt->second);

        accessOrder.push_front (key);
        iterMap[key] = accessOrder.begin();
        return;
    }

    // Evict LRU if at capacity
    if ((int) cache.size() >= maxEntries && ! accessOrder.empty())
    {
        auto lru = accessOrder.back();
        accessOrder.pop_back();
        cache.erase (lru);
        iterMap.erase (lru);
    }

    // Insert new entry
    cache[key] = summary;
    accessOrder.push_front (key);
    iterMap[key] = accessOrder.begin();
}

void AudioAnalysisCache::clear()
{
    const juce::ScopedLock sl (lock);
    cache.clear();
    accessOrder.clear();
    iterMap.clear();
}

} // namespace waive
