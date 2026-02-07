#include "ScreenshotCapture.h"
#include "MainComponent.h"
#include "SessionComponent.h"
#include "TimelineComponent.h"
#include "MixerComponent.h"
#include "ToolSidebarComponent.h"
#include "ChatPanelComponent.h"
#include "LibraryComponent.h"
#include "PluginBrowserComponent.h"
#include "ConsoleComponent.h"
#include "ToolLogComponent.h"

namespace waive
{

juce::Image ScreenshotCapture::resizeIfNeeded (const juce::Image& img, int maxWidth)
{
    if (! img.isValid())
        return img;

    if (img.getWidth() <= maxWidth)
        return img;

    float scale = static_cast<float> (maxWidth) / static_cast<float> (img.getWidth());
    int newH = juce::roundToInt (img.getHeight() * scale);
    return img.rescaled (maxWidth, newH, juce::Graphics::highResamplingQuality);
}

bool ScreenshotCapture::saveImage (const juce::Image& img, const juce::File& path,
                                    const Options& opts)
{
    auto resized = resizeIfNeeded (img, opts.maxWidth);

    if (! resized.isValid())
        return false;

    juce::JPEGImageFormat jpeg;
    jpeg.setQuality (opts.jpegQuality);

    path.getParentDirectory().createDirectory();
    juce::FileOutputStream stream (path);

    if (stream.failedToOpen())
        return false;

    return jpeg.writeImageToStream (resized, stream);
}

int ScreenshotCapture::captureAll (MainComponent& main, const Options& opts)
{
    int count = 0;
    auto grabAndSave = [&] (juce::Component& comp, const juce::String& name) -> bool
    {
        if (comp.getWidth() <= 0 || comp.getHeight() <= 0)
            return false;

        auto img = comp.createComponentSnapshot (comp.getLocalBounds(), true, 1.0f);

        if (! img.isValid())
            return false;

        if (saveImage (img, opts.outputDir.getChildFile (name + ".jpg"), opts))
        {
            ++count;
            return true;
        }
        return false;
    };

    // 1. Full window
    grabAndSave (main, "01_full_window");

    // 2. Session tab components
    auto& session = main.getSessionComponentForTesting();

    grabAndSave (session, "02_session_full");
    grabAndSave (session.getTimeline(), "03_timeline");
    grabAndSave (session.getMixerForTesting(), "04_mixer");

    // Tool sidebar (open it, capture, close it)
    auto* sidebar = session.getToolSidebar();
    bool sidebarWasVisible = sidebar != nullptr && sidebar->isVisible();
    if (sidebar != nullptr && ! sidebarWasVisible)
        session.toggleToolSidebar();
    if (sidebar != nullptr && sidebar->isVisible())
        grabAndSave (*sidebar, "05_tool_sidebar");
    if (sidebar != nullptr && ! sidebarWasVisible)
        session.toggleToolSidebar(); // restore

    // Chat panel (open it, capture, close it)
    auto* chat = session.getChatPanelForTesting();
    bool chatWasVisible = chat != nullptr && chat->isVisible();
    if (chat != nullptr && ! chatWasVisible)
        session.toggleChatPanel();
    if (chat != nullptr && chat->isVisible())
        grabAndSave (*chat, "06_chat_panel");
    if (chat != nullptr && ! chatWasVisible)
        session.toggleChatPanel(); // restore

    // 3. Other tabs — capture via test helper accessors
    grabAndSave (main.getLibraryComponentForTesting(), "07_library");
    grabAndSave (main.getPluginBrowserForTesting(), "08_plugin_browser");

    return count;
}

} // namespace waive
