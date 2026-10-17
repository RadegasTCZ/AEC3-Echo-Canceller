#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/ParamIds.h"
#include "../Source/Strength.h"

namespace aec
{
class UiEditorTests final : public juce::UnitTest
{
public:
    UiEditorTests() : juce::UnitTest ("Editor main view", "UiEditor") {}

    using Proc = AEC3EchoCancellerAudioProcessor;

    static float raw (Proc& p, const char* id) { return p.getAPVTS().getRawParameterValue (id)->load(); }

    static void setRaw (Proc& p, const char* id, float v)
    {
        auto* prm = p.getAPVTS().getParameter (id);
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    }

    void runTest() override
    {
        beginTest ("fixed 440x236, not resizable");
        {
            Proc p;
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            expectEquals (ed->getWidth(), 440);
            expectEquals (ed->getHeight(), 236);
            expect (! ed->isResizable());
        }

        beginTest ("Strong pill applies Strong and lights it");
        {
            Proc p;
            AEC3EchoCancellerAudioProcessorEditor ed (p);
            auto& pills = ed.getEchoBox().getStrengthSelector();
            expectEquals (pills.getLitIndex(), 1);   // defaults are the Normal set
            pills.selectIndex (2);
            expect (aec::matchStrength (p.getAPVTS()) == aec::Strength::Strong);
            expectEquals (pills.getLitIndex(), 2);
            pills.selectIndex (0);
            expectEquals (pills.getLitIndex(), 0);
        }

        beginTest ("moving a tuning slider lights nothing on the main view");
        {
            Proc p;
            AEC3EchoCancellerAudioProcessorEditor ed (p);
            auto& pills = ed.getEchoBox().getStrengthSelector();
            setRaw (p, aec::ids::lfDecay, 40.0f);
            pills.refresh();
            expectEquals (pills.getLitIndex(), -1);
        }

        beginTest ("noise pills follow nsLevel and set it");
        {
            Proc p;
            AEC3EchoCancellerAudioProcessorEditor ed (p);
            auto& level = ed.getNoiseBox().getLevelSelector();
            expectEquals (level.getNumSegments(), 5);
            expectEquals (level.getSelectedIndex(), 2);
            setRaw (p, aec::ids::nsLevel, 4.0f);
            expectEquals (level.getSelectedIndex(), 4);
            level.selectIndex (1);
            expectEquals ((int) raw (p, aec::ids::nsLevel), 1);
            expectEquals (level.getSegmentText (0), juce::String ("Off"));
        }

        beginTest ("Advanced toggles the page and the editor size is unchanged");
        {
            Proc p;
            AEC3EchoCancellerAudioProcessorEditor ed (p);
            expect (! ed.isAdvancedShown());
            expect (ed.getEchoBox().isVisible() && ed.getNoiseBox().isVisible());

            ed.getHeader().getAdvancedButton().setToggleState (true, juce::sendNotificationSync);   // a click
            expect (ed.isAdvancedShown());
            expect (p.isAdvancedOpen());
            expect (! ed.getEchoBox().isVisible() && ! ed.getNoiseBox().isVisible());
            expectEquals (ed.getWidth(), 440);
            expectEquals (ed.getHeight(), 236);

            // persisted: a new editor opens on the Advanced page, and the state survives a save / load
            {
                AEC3EchoCancellerAudioProcessorEditor ed2 (p);
                expect (ed2.isAdvancedShown());
            }
            juce::MemoryBlock state;
            p.getStateInformation (state);
            Proc q;
            q.setStateInformation (state.getData(), (int) state.getSize());
            expect (q.isAdvancedOpen());

            ed.getHeader().getAdvancedButton().setToggleState (false, juce::sendNotificationSync);
            expect (! ed.isAdvancedShown());
            expect (! p.isAdvancedOpen());
            expect (ed.getEchoBox().isVisible() && ed.getNoiseBox().isVisible());
            expectEquals (ed.getHeight(), 236);
        }

        beginTest ("Advanced Echo tab shows 8 sliders, Double-talk 6");
        {
            Proc p;
            AEC3EchoCancellerAudioProcessorEditor ed (p);
            ed.getHeader().getAdvancedButton().setToggleState (true, juce::sendNotificationSync);
            auto& page = ed.getAdvancedPage();
            auto countVisible = [&] (int from, int to)
            {
                int n = 0;
                for (int i = from; i < to; ++i)
                    n += page.getSlider (i).isVisible() ? 1 : 0;
                return n;
            };
            expectEquals (page.getTab(), 0);
            expectEquals (countVisible (0, 8), 8);
            expectEquals (countVisible (8, 14), 0);
            expect (page.getStrengthSelector().isVisible());
            expectEquals (page.getStrengthSelector().getNumPills(), 4);
            expectEquals (page.getStrengthSelector().getWidth(), 4 * 52 + 3 * 4);
            expectEquals (page.getStrengthSelector().getHeight(), 18);

            ed.getHeader().getTabButton (1).setToggleState (true, juce::sendNotificationSync);
            expectEquals (page.getTab(), 1);
            expectEquals (countVisible (0, 8), 0);
            expectEquals (countVisible (8, 14), 6);
            expect (! page.getStrengthSelector().isVisible());
            expect (ed.getHeader().getTabButton (1).getToggleState());

            ed.getHeader().getTabButton (2).setToggleState (true, juce::sendNotificationSync);
            expect (page.getOtherTab().isVisible());
            expectEquals (countVisible (0, 14), 0);
            expectEquals (page.getTab(), 2);
            expect (! ed.getHeader().getTabButton (0).getToggleState() && ! ed.getHeader().getTabButton (1).getToggleState());
            expect (ed.getHeader().getTabButton (2).getToggleState());
        }

        beginTest ("moving a slider lights Custom on the Echo tab");
        {
            Proc p;
            AEC3EchoCancellerAudioProcessorEditor ed (p);
            ed.getHeader().getAdvancedButton().setToggleState (true, juce::sendNotificationSync);
            auto& pills = ed.getAdvancedPage().getStrengthSelector();
            expectEquals (pills.getLitIndex(), 1);
            ed.getAdvancedPage().getSlider (5).setValueAsGesture (40.0f);   // lfDecay
            expectEquals (raw (p, aec::ids::lfDecay), 40.0f);
            pills.refresh();
            expectEquals (pills.getLitIndex(), 3);
        }

        beginTest ("strength click on Advanced stays on Advanced");
        {
            Proc p;
            AEC3EchoCancellerAudioProcessorEditor ed (p);
            ed.getHeader().getAdvancedButton().setToggleState (true, juce::sendNotificationSync);
            ed.getAdvancedPage().getStrengthSelector().selectIndex (2);
            expect (aec::matchStrength (p.getAPVTS()) == aec::Strength::Strong);
            expect (ed.isAdvancedShown() && p.isAdvancedOpen());
            expect (ed.getAdvancedPage().isVisible());
        }

        beginTest ("hovering lfSuppression shows its help title in the strip");
        {
            Proc p;
            AEC3EchoCancellerAudioProcessorEditor ed (p);
            ed.getHeader().getAdvancedButton().setToggleState (true, juce::sendNotificationSync);
            auto& page = ed.getAdvancedPage();
            expectEquals (page.getHelpTitle(), juce::String());
            page.getSlider (0).onHoverOrClick (page.getSlider (0).getParamId());
            expectEquals (page.getSlider (0).getParamId(), juce::String (aec::ids::lfSuppression));
            expectEquals (page.getHelpTitle(), juce::String ("LF Suppression"));
        }

        beginTest ("each CompactSlider tick sits at its Normal value");
        {
            Proc p;
            AEC3EchoCancellerAudioProcessorEditor ed (p);
            const auto& normal = aec::tuningFor (aec::Strength::Normal);
            for (int i = 0; i < aec::kNumTuningParams; ++i)
            {
                auto& slider = ed.getAdvancedPage().getSlider (i);
                expectEquals (slider.getParamId(), juce::String (aec::kTuningParamIds[(size_t) i]));
                expectEquals (slider.getNormalValue(), normal[(size_t) i]);
                const auto track = slider.getTrackBounds();
                const int expectedX = track.getX() + juce::roundToInt (normal[(size_t) i] / 100.0f * (float) (track.getWidth() - 2));
                expectEquals (slider.getTickX(), expectedX);
            }
        }

        beginTest ("factory reset restores defaults incl nsLevel 12 and leaves Advanced open");
        {
            Proc p;
            AEC3EchoCancellerAudioProcessorEditor ed (p);
            ed.getHeader().getAdvancedButton().setToggleState (true, juce::sendNotificationSync);
            ed.getHeader().getTabButton (2).setToggleState (true, juce::sendNotificationSync);
            setRaw (p, aec::ids::lfDecay, 40.0f);
            setRaw (p, aec::ids::latencyComp, 50.0f);
            setRaw (p, aec::ids::roomReverb, 30.0f);
            setRaw (p, aec::ids::nsLevel, 4.0f);
            setRaw (p, aec::ids::boundedErl, 1.0f);
            setRaw (p, aec::ids::clockDrift, 1.0f);
            setRaw (p, aec::ids::monitorCapture, 1.0f);
            setRaw (p, aec::ids::monitorNoise, 1.0f);
            setRaw (p, aec::ids::aecEnabled, 0.0f);

            ed.getAdvancedPage().getOtherTab().getFactoryResetButton().onClick();

            expect (aec::matchStrength (p.getAPVTS()) == aec::Strength::Normal);
            expectEquals (raw (p, aec::ids::latencyComp), 0.0f);
            expectEquals (raw (p, aec::ids::roomReverb), 13.0f);
            expectEquals ((int) raw (p, aec::ids::nsLevel), 2);
            expect (raw (p, aec::ids::boundedErl) < 0.5f && raw (p, aec::ids::clockDrift) < 0.5f);
            expect (raw (p, aec::ids::monitorCapture) < 0.5f && raw (p, aec::ids::monitorNoise) < 0.5f);
            expect (raw (p, aec::ids::aecEnabled) >= 0.5f);
            expectEquals (p.getLoopbackCapture().getDeviceId(), juce::String());
            expect (ed.isAdvancedShown() && p.isAdvancedOpen());
            expectEquals (ed.getAdvancedPage().getTab(), 2);
        }

        beginTest ("factory reset wraps every parameter change (incl nsLevel) in a change gesture");
        {
            Proc p;
            AEC3EchoCancellerAudioProcessorEditor ed (p);
            setRaw (p, aec::ids::nsLevel, 4.0f);

            struct GestureCounter final : juce::AudioProcessorListener
            {
                int open = 0, changesOutsideGesture = 0, nsLevelChanges = 0, nsLevelIndex = -1;
                void audioProcessorParameterChanged (juce::AudioProcessor*, int index, float) override
                {
                    if (open == 0)
                        ++changesOutsideGesture;
                    if (index == nsLevelIndex)
                        ++nsLevelChanges;
                }
                void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
                void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int) override { ++open; }
                void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int) override   { --open; }
            } counter;
            counter.nsLevelIndex = p.getAPVTS().getParameter (aec::ids::nsLevel)->getParameterIndex();

            p.addListener (&counter);
            ed.getAdvancedPage().getOtherTab().getFactoryResetButton().onClick();
            p.removeListener (&counter);

            expectEquals (counter.nsLevelChanges, 1);
            expectEquals (counter.changesOutsideGesture, 0);
            expectEquals (counter.open, 0);
        }

        beginTest ("close button returns to main view");
        {
            Proc p;
            AEC3EchoCancellerAudioProcessorEditor ed (p);
            ed.getHeader().getAdvancedButton().setToggleState (true, juce::sendNotificationSync);
            expect (ed.getHeader().getCloseButton().isVisible());
            expect (! ed.getHeader().getDeviceCombo().isVisible());
            ed.getHeader().getCloseButton().onClick();
            expect (! ed.isAdvancedShown() && ! p.isAdvancedOpen());
            expect (ed.getEchoBox().isVisible() && ed.getNoiseBox().isVisible());
            expect (! ed.getAdvancedPage().isVisible());
            expect (ed.getHeader().getDeviceCombo().isVisible() && ed.getHeader().getAdvancedButton().isVisible());
            expect (! ed.getHeader().getCloseButton().isVisible());
        }

        beginTest ("window size never changes across all tabs");
        {
            Proc p;
            AEC3EchoCancellerAudioProcessorEditor ed (p);
            ed.getHeader().getAdvancedButton().setToggleState (true, juce::sendNotificationSync);
            for (int tab = 0; tab < 3; ++tab)
            {
                ed.getHeader().getTabButton (tab).setToggleState (true, juce::sendNotificationSync);
                expectEquals (ed.getWidth(), 440);
                expectEquals (ed.getHeight(), 236);
                expect (ed.getAdvancedPage().getBounds() == juce::Rectangle<int> (8, 44, 424, 184));
            }
        }

        beginTest ("double-talk sliders show DT help; switching tabs clears the strip");
        {
            Proc p;
            AEC3EchoCancellerAudioProcessorEditor ed (p);
            ed.getHeader().getAdvancedButton().setToggleState (true, juce::sendNotificationSync);
            auto& page = ed.getAdvancedPage();
            ed.getHeader().getTabButton (1).setToggleState (true, juce::sendNotificationSync);
            for (int i = 8; i < 14; ++i)
            {
                page.getSlider (i).onHoverOrClick (page.getSlider (i).getParamId());
                expect (page.getHelpTitle().startsWith ("DT"), "slider " + juce::String (i) + ": " + page.getHelpTitle());
            }
            ed.getHeader().getTabButton (0).setToggleState (true, juce::sendNotificationSync);
            expectEquals (page.getHelpTitle(), juce::String());
            page.getSlider (0).onHoverOrClick (page.getSlider (0).getParamId());
            expect (! page.getHelpTitle().startsWith ("DT"));
            ed.getHeader().getTabButton (1).setToggleState (true, juce::sendNotificationSync);
            expectEquals (page.getHelpTitle(), juce::String());
        }

        beginTest ("AEC3 Error shows the Factory reset hint and a reset clears it");
        {
            Proc p;
            p.prepareToPlay (48000.0, 480);
            AEC3EchoCancellerAudioProcessorEditor ed (p);
            auto& engine = p.getEchoCancellerEngine();
            engine.setErrorForTesting (true);
            ed.updateConnection();
            expectEquals (ed.getAdvancedPage().getOtherTab().getStatusText(), juce::String ("AEC3 Error - use Factory reset"));
            expectEquals (ed.getEchoBox().getConnectionTooltip(), juce::String ("AEC3 Error - use Factory reset (Advanced > Other)"));
            expect (ed.getEchoBox().getLinkState() == aec::ui::LinkState::Problem);

            juce::AudioBuffer<float> buf (2, 480);
            buf.clear();
            juce::MidiBuffer midi;
            p.processBlock (buf, midi);   // the halted engine pauses the reference capture
            expect (! p.getLoopbackCapture().isCaptureActive());

            ed.getAdvancedPage().getOtherTab().getFactoryResetButton().onClick();
            for (int i = 0; i < 4; ++i)
                p.processBlock (buf, midi);
            expect (! engine.hasError());
            expect (p.getLoopbackCapture().isCaptureActive(), "reference capture still paused after the reset");
            ed.updateConnection();
            expect (ed.getAdvancedPage().getOtherTab().getStatusText() != juce::String ("AEC3 Error - use Factory reset"));
        }

        beginTest ("a stopped capture shows Disconnected, not Error");
        {
            Proc p;   // never prepared: the capture is Disconnected, as after releaseResources / stopCapture
            AEC3EchoCancellerAudioProcessorEditor ed (p);
            ed.updateConnection();
            expectEquals (ed.getAdvancedPage().getOtherTab().getStatusText(), juce::String ("Disconnected"));
            expectEquals (ed.getEchoBox().getConnectionTooltip(), juce::String ("Disconnected"));
            expect (ed.getEchoBox().getLinkState() != aec::ui::LinkState::Problem);
        }

        beginTest ("On pill bound to aecEnabled");
        {
            Proc p;
            AEC3EchoCancellerAudioProcessorEditor ed (p);
            auto& on = ed.getHeader().getOnButton();
            expect (on.getToggleState());
            setRaw (p, aec::ids::aecEnabled, 0.0f);
            expect (! on.getToggleState());
            on.setToggleState (true, juce::sendNotificationSync);   // a click
            expect (raw (p, aec::ids::aecEnabled) >= 0.5f);
        }

        beginTest ("device combo first item is System Default and the tooltip text matches");
        {
            Proc p;
            AEC3EchoCancellerAudioProcessorEditor ed (p);
            auto& combo = ed.getHeader().getDeviceCombo();
            expect (combo.getNumItems() >= 1);
            expectEquals (combo.getItemText (0), juce::String ("System Default"));
            expectEquals (combo.getTooltip(),
                          juce::String ("Reference: what the plugin listens to as the echo source"));
        }

        beginTest ("rotary value text: Delay in ms, Room as raw blocks");
        {
            Proc p;
            AEC3EchoCancellerAudioProcessorEditor ed (p);
            setRaw (p, aec::ids::latencyComp, 50.0f);
            setRaw (p, aec::ids::roomReverb, 20.0f);
            expectEquals (ed.getEchoBox().getDelayKnob().getTextFromValue (50.0), juce::String ("50 ms"));
            expectEquals (ed.getEchoBox().getRoomKnob().getTextFromValue (20.0), juce::String ("20"));
        }
    }
};

static UiEditorTests uiEditorTests;
} // namespace aec
