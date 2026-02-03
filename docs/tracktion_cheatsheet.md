# Tracktion Engine API Cheat Sheet

Quick reference for the Tracktion Engine APIs used in Waive.

## Engine & Edit Lifecycle

```cpp
te::Engine engine { "Waive" };
auto edit = te::createEmptyEdit(engine);
// or: te::loadEditFromFile(engine, editFile);
```

## Track Operations

```cpp
auto track = edit->getOrInsertAudioTrackAt(0);  // get or create
auto tracks = te::getAudioTracks(*edit);         // list all
```

## Clip Insertion

```cpp
// Audio clip
te::AudioFile audioFile(edit->engine, file);
auto clip = track->insertWaveClip(name, file,
    { { startTime, startTime + audioFile.getLength() }, 0.0 }, false);

// MIDI/Step clip
track->insertNewClip(te::TrackItem::Type::step, "Step Clip", timeRange, nullptr);
```

## Volume & Pan

```cpp
// VolumeAndPanPlugin is auto-inserted on each track
auto volPlugin = track->pluginList.getPluginsOfType<te::VolumeAndPanPlugin>().getFirst();
volPlugin->volParam->setParameter(te::decibelsToVolumeFaderPosition(valueDb), juce::sendNotification);
volPlugin->panParam->setParameter((pan + 1.0f) / 2.0f, juce::sendNotification);  // [-1,1] → [0,1]
```

## Plugin Parameters

```cpp
auto param = plugin->getAutomatableParameterByID("paramId");
param->setParameter(newValue, juce::sendNotification);
float current = param->getCurrentValue();
```

## Transport

```cpp
auto& transport = edit->getTransport();
transport.play(false);
transport.stop(false, false);
transport.setCurrentPosition(seconds);
```

## Object Hierarchy

```
te::Engine
  └── te::Edit (session — backed by juce::ValueTree / XML)
        ├── te::TempoSequence
        ├── te::AudioTrack[]
        │     ├── te::Clip[] (WaveAudioClip, MidiClip, StepClip)
        │     └── te::PluginList
        │           ├── te::VolumeAndPanPlugin (auto-inserted)
        │           └── te::ExternalPlugin[] (VST3/AU)
        │                 └── te::AutomatableParameter[]
        └── te::TransportControl
```
