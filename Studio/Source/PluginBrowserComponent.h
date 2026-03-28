/*
  ==============================================================================

    PluginBrowserComponent.h  —  M8 VST/AU Plugin Hosting
    Floating window for browsing the scanned plugin list and selecting one.
    Inline implementation (header-only).

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>
#include "PluginManager.h"

// ---------------------------------------------------------------------------
// PluginBrowserComponent — the inner content

class PluginBrowserComponent : public juce::Component,
                               public juce::ListBoxModel
{
public:
    std::function<void(const juce::PluginDescription&)> onPluginSelected;

    PluginBrowserComponent()
    {
        searchField.setTextToShowWhenEmpty("Search plugins...", juce::Colour(0xff555560));
        searchField.onTextChange = [this] { refreshList(); };
        addAndMakeVisible(searchField);

        pluginList.setModel(this);
        pluginList.setRowHeight(24);
        pluginList.setColour(juce::ListBox::backgroundColourId,
                             juce::Colour(0xff1a1a20));
        addAndMakeVisible(pluginList);

        scanBtn.setButtonText("Scan for Plugins");
        scanBtn.onClick = [this] { startScan(); };
        addAndMakeVisible(scanBtn);

        loadBtn.setButtonText("Load");
        loadBtn.onClick = [this] { loadSelected(); };
        addAndMakeVisible(loadBtn);

        statusLabel.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
        statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff888892));
        addAndMakeVisible(statusLabel);

        refreshList();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff1c1c1e));
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced(8);
        searchField.setBounds(r.removeFromTop(28));
        r.removeFromTop(6);
        statusLabel.setBounds(r.removeFromBottom(18));
        r.removeFromBottom(4);
        auto btns = r.removeFromBottom(28);
        r.removeFromBottom(4);
        pluginList.setBounds(r);
        loadBtn.setBounds(btns.removeFromRight(80));
        btns.removeFromRight(4);
        scanBtn.setBounds(btns.removeFromRight(150));
    }

    // --- ListBoxModel -------------------------------------------------------

    int getNumRows() override { return (int)filtered.size(); }

    void paintListBoxItem(int row, juce::Graphics& g, int w, int h,
                          bool selected) override
    {
        if (row < 0 || row >= (int)filtered.size()) return;
        const auto& desc = filtered[(size_t)row];

        g.fillAll(selected ? juce::Colour(0xff3a3a50)
                           : (row % 2 == 0 ? juce::Colour(0xff222228)
                                           : juce::Colour(0xff1e1e24)));

        g.setColour(juce::Colour(0xffe0e0e8));
        g.setFont(juce::Font(juce::FontOptions().withHeight(12.5f)));
        g.drawText(desc.name, 8, 0, w / 2 - 4, h, juce::Justification::centredLeft);

        g.setColour(juce::Colour(0xff888892));
        g.setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
        g.drawText(desc.manufacturerName + "  |  " + desc.pluginFormatName,
                   w / 2, 0, w / 2 - 8, h, juce::Justification::centredRight);
    }

    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override
    {
        loadAtRow(row);
    }

private:
    juce::TextEditor searchField;
    juce::ListBox    pluginList { "Plugins", this };
    juce::TextButton scanBtn, loadBtn;
    juce::Label      statusLabel;

    std::vector<juce::PluginDescription> filtered;   // copies, not pointers

    void refreshList()
    {
        const juce::String filter = searchField.getText().toLowerCase();
        filtered.clear();

        for (const auto& desc : PluginManager::getInstance().getKnownPlugins().getTypes())
        {
            if (desc.name.isEmpty()) continue;        // skip corrupted entries
            if (!desc.isInstrument) continue;          // only show instrument plugins
            if (filter.isEmpty()
                || desc.name.containsIgnoreCase(filter)
                || desc.manufacturerName.containsIgnoreCase(filter))
                filtered.push_back(desc);
        }

        pluginList.updateContent();
        pluginList.repaint();
        statusLabel.setText(juce::String(filtered.size()) + " plugin(s)",
                            juce::dontSendNotification);
    }

    void startScan()
    {
        if (PluginManager::getInstance().isScanning()) return;

        scanBtn.setEnabled(false);
        scanBtn.setButtonText("Scanning...");

        PluginManager::getInstance().scanPlugins(
            [this](float progress, const juce::String& name)
            {
                statusLabel.setText(juce::String((int)(progress * 100)) + "%  " + name,
                                    juce::dontSendNotification);
            },
            [this]()
            {
                scanBtn.setEnabled(true);
                scanBtn.setButtonText("Scan for Plugins");
                refreshList();
            });
    }

    void loadSelected() { loadAtRow(pluginList.getSelectedRow()); }

    void loadAtRow(int row)
    {
        if (row < 0 || row >= (int)filtered.size()) return;
        if (onPluginSelected)
            onPluginSelected(filtered[(size_t)row]);
    }
};

// ---------------------------------------------------------------------------
// PluginBrowserWindow — floating DocumentWindow wrapper

class PluginBrowserWindow : public juce::DocumentWindow
{
public:
    // Called when the user double-clicks or presses Load.
    // Provides the selected channel index and plugin description.
    std::function<void(int ch, const juce::PluginDescription&)> onPluginSelected;

    PluginBrowserWindow()
        : juce::DocumentWindow("Plugin Browser",
                               juce::Colour(0xff1c1c1e),
                               juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        setResizable(true, false);
        setContentOwned(new PluginBrowserComponent(), true);
        setSize(540, 440);
        centreWithSize(getWidth(), getHeight());
    }

    // Show the browser targeting a specific channel.
    void showForChannel(int ch)
    {
        targetChannel = ch;
        if (auto* content = dynamic_cast<PluginBrowserComponent*>(getContentComponent()))
        {
            content->onPluginSelected = [this](const juce::PluginDescription& desc)
            {
                if (onPluginSelected)
                    onPluginSelected(targetChannel, desc);
                setVisible(false);
            };
        }
        setVisible(true);
        toFront(true);
    }

    int  getTargetChannel() const { return targetChannel; }
    void closeButtonPressed() override { setVisible(false); }

private:
    int targetChannel = -1;
};

// ---------------------------------------------------------------------------
// PluginEditorWindow — wraps a plugin's native UI

class PluginEditorWindow : public juce::DocumentWindow
{
public:
    PluginEditorWindow(juce::AudioPluginInstance& plugin, int ch)
        : juce::DocumentWindow(plugin.getName() + "  [Ch " + juce::String(ch + 1) + "]",
                               juce::Colour(0xff1c1c1e),
                               juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        setResizable(true, false);

        if (auto* editor = plugin.createEditor())
        {
            setContentOwned(editor, true);
            setSize(editor->getWidth()  > 0 ? editor->getWidth()  : 500,
                    editor->getHeight() > 0 ? editor->getHeight() : 400);
        }
        else
        {
            // Plugin has no custom UI — show a plain placeholder
            auto* placeholder = new juce::Label({}, plugin.getName() + "\n(no editor)");
            placeholder->setJustificationType(juce::Justification::centred);
            placeholder->setColour(juce::Label::textColourId, juce::Colour(0xffb0b0b8));
            setContentOwned(placeholder, true);
            setSize(300, 120);
        }

        centreWithSize(getWidth(), getHeight());
    }

    void closeButtonPressed() override { setVisible(false); }
};
