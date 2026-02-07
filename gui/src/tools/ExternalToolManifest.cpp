#include "ExternalToolManifest.h"

namespace waive
{

std::optional<ExternalToolManifest> parseManifest (const juce::File& manifestFile)
{
    if (! manifestFile.existsAsFile())
        return std::nullopt;

    // Restrict manifest location to prevent path traversal
    auto canonicalPath = manifestFile.getParentDirectory().getFullPathName();
    if (canonicalPath.contains ("..") || canonicalPath.startsWith ("/etc") ||
        canonicalPath.startsWith ("/root") || canonicalPath.startsWith ("/sys") ||
        canonicalPath.startsWith ("/proc"))
        return std::nullopt;

    auto text = manifestFile.loadFileAsString();
    auto parsed = juce::JSON::parse (text);
    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr)
        return std::nullopt;

    ExternalToolManifest manifest;

    // Required fields
    if (! obj->hasProperty ("name") || ! obj->hasProperty ("executable"))
        return std::nullopt;

    manifest.name = obj->getProperty ("name").toString().trim();
    manifest.executable = obj->getProperty ("executable").toString().trim();

    if (manifest.name.isEmpty() || manifest.executable.isEmpty())
        return std::nullopt;

    // Validate executable - only allow known safe interpreters and reject paths with shell metacharacters
    const juce::StringArray allowedExecutables = { "python3", "python", "node", "ruby", "perl" };
    bool executableAllowed = false;
    for (const auto& allowed : allowedExecutables)
    {
        if (manifest.executable == allowed)
        {
            executableAllowed = true;
            break;
        }
    }
    if (! executableAllowed)
        return std::nullopt;

    // Reject arguments containing shell metacharacters or destructive patterns
    const juce::StringArray dangerousArgumentPatterns = { ";", "&", "|", "`", "$", "(", ")", "<", ">",
                                                           "rm ", "dd ", "curl ", "/dev/", "--force", "-f " };
    for (const auto& arg : manifest.arguments)
    {
        for (const auto& pattern : dangerousArgumentPatterns)
        {
            if (arg.contains (pattern))
                return std::nullopt;
        }
    }

    // Optional fields
    if (obj->hasProperty ("displayName"))
        manifest.displayName = obj->getProperty ("displayName").toString();
    else
        manifest.displayName = manifest.name;

    if (obj->hasProperty ("version"))
        manifest.version = obj->getProperty ("version").toString();
    else
        manifest.version = "1.0.0";

    if (obj->hasProperty ("description"))
        manifest.description = obj->getProperty ("description").toString();

    if (obj->hasProperty ("inputSchema"))
        manifest.inputSchema = obj->getProperty ("inputSchema");

    if (obj->hasProperty ("defaultParams"))
        manifest.defaultParams = obj->getProperty ("defaultParams");

    if (obj->hasProperty ("arguments"))
    {
        auto argsVar = obj->getProperty ("arguments");
        if (auto* argsArray = argsVar.getArray())
        {
            for (const auto& argVar : *argsArray)
            {
                auto arg = argVar.toString();
                // Validate argument safety
                const juce::StringArray dangerousPatterns = { ";", "&", "|", "`", "$", "(", ")", "<", ">",
                                                               "rm ", "dd ", "curl ", "/dev/", "--force", "-f " };
                bool argSafe = true;
                for (const auto& pattern : dangerousPatterns)
                {
                    if (arg.contains (pattern))
                    {
                        argSafe = false;
                        break;
                    }
                }
                if (argSafe)
                    manifest.arguments.add (arg);
                else
                    return std::nullopt;  // Reject entire manifest if any arg is unsafe
            }
        }
    }

    if (obj->hasProperty ("timeoutMs"))
    {
        auto timeoutValue = obj->getProperty ("timeoutMs");
        // Prevent integer overflow - clamp to reasonable range
        if ((int64) timeoutValue < 0 || (int64) timeoutValue > 3600000)  // max 1 hour
            return std::nullopt;
        manifest.timeoutMs = juce::jmax (0, (int) timeoutValue);
    }

    if (obj->hasProperty ("acceptsAudioInput"))
        manifest.acceptsAudioInput = (bool) obj->getProperty ("acceptsAudioInput");

    if (obj->hasProperty ("producesAudioOutput"))
        manifest.producesAudioOutput = (bool) obj->getProperty ("producesAudioOutput");

    manifest.baseDirectory = manifestFile.getParentDirectory();

    return manifest;
}

std::vector<ExternalToolManifest> scanToolDirectory (const juce::File& directory)
{
    std::vector<ExternalToolManifest> manifests;

    if (! directory.isDirectory())
        return manifests;

    juce::Array<juce::File> results;
    directory.findChildFiles (results, juce::File::findFiles, false, "*.waive-tool.json");

    for (const auto& file : results)
    {
        auto manifest = parseManifest (file);
        if (manifest.has_value())
            manifests.push_back (*manifest);
    }

    return manifests;
}

} // namespace waive
