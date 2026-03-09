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
            knownPlugins.recreateFromXml(*xml);
    }
}

// ---------------------------------------------------------------------------
// Initialise

void PluginManager::initialise()
{
    loadList();
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
