/*
  ==============================================================================

    PluginManager.cpp  —  M8 VST/AU Plugin Hosting

  ==============================================================================
*/

#include "PluginManager.h"

// ---------------------------------------------------------------------------
// Singleton

PluginManager& PluginManager::getInstance()
{
    static PluginManager instance;
    return instance;
}

PluginManager::PluginManager()
{
    juce::addDefaultFormatsToManager(formatManager);  // VST3 + AU on macOS
}

PluginManager::~PluginManager()
{
    if (scanThread != nullptr)
        scanThread->stopThread(3000);
}

// ---------------------------------------------------------------------------
// ApplicationProperties helper

juce::PropertiesFile::Options PluginManager::getPropsOptions()
{
    juce::PropertiesFile::Options opts;
    opts.applicationName     = "StudioDAW";
    opts.filenameSuffix      = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    return opts;
}

// ---------------------------------------------------------------------------
// Persist plugin list

void PluginManager::saveList()
{
    juce::ApplicationProperties props;
    props.setStorageParameters(getPropsOptions());

    if (auto* pf = props.getUserSettings())
    {
        if (auto xml = knownPlugins.createXml())
            pf->setValue("knownPlugins", xml.get());
        pf->saveIfNeeded();
    }
}

void PluginManager::loadList()
{
    juce::ApplicationProperties props;
    props.setStorageParameters(getPropsOptions());

    if (auto* pf = props.getUserSettings())
    {
        if (auto xml = pf->getXmlValue("knownPlugins"))
        {
            juce::KnownPluginList tempList;
            tempList.recreateFromXml(*xml);

            // Only keep entries with a valid name — discard corrupted ones
            bool skipped = false;
            for (const auto& desc : tempList.getTypes())
            {
                if (desc.name.isEmpty()) { skipped = true; continue; }
                knownPlugins.addType(desc);
            }

            // Persist cleaned list if we dropped bad entries
            if (skipped) saveList();
        }
    }
}

// ---------------------------------------------------------------------------
// Directory fingerprint — sum of modification times of all plugin files

juce::int64 PluginManager::computeDirectoryFingerprint() const
{
    juce::int64 fp = 0;
    for (auto* format : formatManager.getFormats())
    {
        const juce::FileSearchPath searchPath = format->getDefaultLocationsToSearch();
        for (int i = 0; i < searchPath.getNumPaths(); ++i)
        {
            const auto dir = searchPath[i];
            if (!dir.isDirectory()) continue;
            for (const auto& f : dir.findChildFiles(juce::File::findFilesAndDirectories, false))
            {
                const auto ext = f.getFileExtension().toLowerCase();
                if (ext == ".vst3" || ext == ".component" || ext == ".vst")
                    fp += f.getLastModificationTime().toMilliseconds();
            }
        }
    }
    return fp;
}

void PluginManager::saveFingerprint(juce::int64 fp)
{
    juce::ApplicationProperties props;
    props.setStorageParameters(getPropsOptions());
    if (auto* pf = props.getUserSettings())
    {
        pf->setValue("pluginDirFingerprint", juce::String(fp));
        pf->saveIfNeeded();
    }
}

juce::int64 PluginManager::loadFingerprint() const
{
    juce::ApplicationProperties props;
    props.setStorageParameters(getPropsOptions());
    if (auto* pf = props.getUserSettings())
        return pf->getValue("pluginDirFingerprint", "0").getLargeIntValue();
    return 0;
}

bool PluginManager::autoRescanIfNeeded(std::function<void()> completeCb)
{
    const juce::int64 currentFP = computeDirectoryFingerprint();
    const juce::int64 savedFP   = loadFingerprint();

    if (currentFP != savedFP)
    {
        DBG(juce::String::fromUTF8("Plugin directory changed \xe2\x80\x94 auto-rescanning..."));
        knownPlugins.clear();
        scanPlugins(
            nullptr,
            [this, currentFP, completeCb]()
            {
                saveFingerprint(currentFP);
                if (completeCb) completeCb();
            });
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Initialise

void PluginManager::initialise()
{
    loadList();
    autoRescanIfNeeded();
}

// ---------------------------------------------------------------------------
// Background scan

void PluginManager::scanPlugins(
    std::function<void(float, const juce::String&)> progressCb,
    std::function<void()>                            completeCb)
{
    if (scanning.load()) return;
    scanning = true;

    if (scanThread != nullptr)
        scanThread->stopThread(2000);

    scanThread = std::make_unique<ScanThread>(*this,
                                              std::move(progressCb),
                                              std::move(completeCb));
    scanThread->startThread(juce::Thread::Priority::background);
}

void PluginManager::cancelScan()
{
    if (scanThread != nullptr)
        scanThread->signalThreadShouldExit();
}

void PluginManager::ScanThread::run()
{
    for (auto* format : owner.formatManager.getFormats())
    {
        if (threadShouldExit()) break;

        const juce::FileSearchPath searchPath = format->getDefaultLocationsToSearch();
        juce::PluginDirectoryScanner scanner(owner.knownPlugins, *format,
                                             searchPath, true, juce::File{}, false);

        juce::String pluginName;
        while (!threadShouldExit())
        {
            if (onProgress)
            {
                const float        p    = scanner.getProgress();
                const juce::String name = pluginName;
                juce::MessageManager::callAsync([p, name, cb = onProgress] { cb(p, name); });
            }

            if (!scanner.scanNextFile(true, pluginName))
                break;
        }
    }

    owner.saveList();
    owner.saveFingerprint(owner.computeDirectoryFingerprint());
    owner.scanning = false;

    if (onComplete)
        juce::MessageManager::callAsync([cb = onComplete] { cb(); });
}

// ---------------------------------------------------------------------------
// Create instance

std::unique_ptr<juce::AudioPluginInstance>
PluginManager::createPlugin(const juce::PluginDescription& desc,
                             double sampleRate, int blockSize,
                             juce::String& errorMsg)
{
    return formatManager.createPluginInstance(desc, sampleRate, blockSize, errorMsg);
}
