#pragma once
#include "../PluginProcessor.h"
#include "../Strength.h"
#include "../AgcPresets.h"
#include "AgcPresetSelector.h"
#include "CompactSlider.h"
#include "OtherTab.h"
#include "StrengthSelector.h"
#include <array>

namespace aec::ui
{
/** The Advanced page: a 424 x 184 box at (8, 44) with four tabs, switched from the header pills.
    Echo: Strength selector (with Custom) + 8 compact sliders. Double-talk: 6 compact sliders.
    Level: AGC preset row (with Custom) + 4 compact sliders. Other: OtherTab.
    The three slider tabs share a help strip that explains the last hovered or clicked slider. */
class AdvancedPage final : public juce::Component
{
public:
    static constexpr int kWidth = 424, kHeight = 280;
    enum Tab { Echo = 0, DoubleTalk = 1, Level = 2, Other = 3 };

    explicit AdvancedPage (AEC3EchoCancellerAudioProcessor&);

    void setTab (int tab);
    int getTab() const noexcept                       { return tab; }
    /** Editor timer (30 Hz), only while the page is shown. */
    void tick();

    StrengthSelector& getStrengthSelector() noexcept  { return strength; }
    AgcPresetSelector& getAgcSelector() noexcept      { return agc; }
    OtherTab& getOtherTab() noexcept                  { return otherTab; }
    /** The slider of AGC parameter `index` (aec::kAgcParamIds order). */
    CompactSlider& getAgcSlider (int index)           { return *agcSliders[(size_t) index]; }
    /** The slider of tuning parameter `index` (aec::kTuningParamIds order: 0-7 Echo tab, 8-13 Double-talk tab). */
    CompactSlider& getSlider (int index)              { return *sliders[(size_t) index]; }
    /** Title shown in the help strip ("" while nothing was hovered or clicked). */
    juce::String getHelpTitle() const noexcept        { return helpTitle; }
    /** What the strip says while nothing was hovered or clicked (the Level tab adds the limiter-off clipping note). */
    juce::String getIdleHelpText() const;
    /** Shows the help entry of a parameter in the strip. */
    void showHelp (const juce::String& paramId);

    void paint (juce::Graphics&) override;

private:
    juce::Rectangle<int> helpArea() const { return { 11, kHeight - 49, 402, 46 }; }

    AEC3EchoCancellerAudioProcessor& processor;
    StrengthSelector strength;
    std::array<std::unique_ptr<CompactSlider>, aec::kNumTuningParams> sliders;
    AgcPresetSelector agc;
    std::array<std::unique_ptr<CompactSlider>, aec::kNumAgcParams> agcSliders;
    OtherTab otherTab;

    int tab = Echo;
    juce::String helpTitle, helpBody;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AdvancedPage)
};
} // namespace aec::ui
