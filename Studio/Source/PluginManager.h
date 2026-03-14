/*
  ==============================================================================

    PluginManager.h  —  M8 VST/AU Plugin Hosting
    Singleton that manages plugin format scanning and instance creation.
    All public methods must be called on the message thread.

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>

class PluginManager
{
public:
    // Singleton access — call only from the message thread.
    static PluginManager& getInstance();

    // Register formats + load cached plugin list. Call once at app startup.
    void initialise();

    // Start a background scan of default VST/AU search paths.
    // progressCb(progress 0..1, currentPluginName) fires on the message thread.
    // completeCb() fires on the message thread when finished.
    void scanPlugins(std::function<void(float, const juce::String&)> progressCb,
                     std::function<void()>                            completeCb);

    void cancelScan();
    bool isScanning() const { return scanning.load(); }

    const juce::KnownPluginList&    getKnownPlugins()  const { return knownPlugins; }
    juce::AudioPluginFormatManager& getFormatManager()       { return formatManager; }

    // Create and return a plugin instance. Returns nullptr on failure; sets errorMsg.
    std::unique_ptr<juce::AudioPluginInstance> createPlugin(
        const juce::PluginDescription& desc,
        double sampleRate, int blockSize,
        juce::String& errorMsg);

    // Persist / restore the scanned plugin list via ApplicationProperties.
    void saveList();
    void loadList();

private:
    PluginManager();
    ~PluginManager();

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList          knownPlugins;
    std::atomic<bool>              scanning { false };

    struct ScanThread : public juce::Thread
    {
        ScanThread(PluginManager& pm,
                   std::function<void(float, const juce::String&)> prog,
                   std::function<void()>                           done)
            : juce::Thread("PluginScanner"), owner(pm),
              onProgress(std::move(prog)), onComplete(std::move(done)) {}

        void run() override;

        PluginManager& owner;
        std::function<void(float, const juce::String&)> onProgress;
        std::function<void()>                           onComplete;
    };

    std::unique_ptr<ScanThread> scanThread;

    static juce::PropertiesFile::Options getPropsOptions();

    JUCE_DECLARE_NON_COPYABLE(PluginManager)
};
