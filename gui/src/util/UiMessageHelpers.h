#pragma once

#include <JuceHeader.h>
#include <cstdlib>

namespace waive
{

inline bool asyncDialogsDisabled()
{
    if (const auto* value = std::getenv ("WAIVE_DISABLE_ASYNC_DIALOGS"))
        return *value != '\0' && juce::String (value).trim().compareIgnoreCase ("0") != 0;

    return false;
}

inline bool isHeadlessUiEnvironment()
{
#if JUCE_LINUX || JUCE_BSD
    const auto hasDisplayEnv = [] (const char* name)
    {
        if (const auto* value = std::getenv (name))
            return *value != '\0';

        return false;
    };

    return ! hasDisplayEnv ("DISPLAY") && ! hasDisplayEnv ("WAYLAND_DISPLAY");
#else
    return false;
#endif
}

inline void showMessageBoxAsyncSafe (juce::MessageBoxIconType iconType,
                                     const juce::String& title,
                                     const juce::String& message,
                                     juce::Component* associatedComponent = nullptr)
{
    juce::ignoreUnused (iconType, associatedComponent);

    if (asyncDialogsDisabled() || isHeadlessUiEnvironment())
    {
        juce::Logger::writeToLog (title + ": " + message);
        return;
    }

    juce::AlertWindow::showMessageBoxAsync (iconType, title, message, {}, associatedComponent);
}

inline void showNativeMessageBoxAsyncSafe (juce::MessageBoxIconType iconType,
                                           const juce::String& title,
                                           const juce::String& message,
                                           juce::Component* associatedComponent = nullptr)
{
    juce::ignoreUnused (iconType, associatedComponent);

    if (asyncDialogsDisabled() || isHeadlessUiEnvironment())
    {
        juce::Logger::writeToLog (title + ": " + message);
        return;
    }

    juce::NativeMessageBox::showMessageBoxAsync (iconType, title, message, associatedComponent);
}

} // namespace waive
