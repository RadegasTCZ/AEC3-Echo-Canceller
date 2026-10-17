#include <juce_audio_processors/juce_audio_processors.h>
#include "../Source/PeakHold.h"
#include "../Source/UI/HelpText.h"
#include "../Source/UI/MeterBallistics.h"
#include "../Source/UI/MeterView.h"
#include "../Source/UI/SegmentedSelector.h"
#include "../Source/UI/Theme.h"
#include <cmath>
#include <cstdlib>
#include <limits>

namespace aec
{
class UiThemeTests final : public juce::UnitTest
{
public:
    UiThemeTests() : juce::UnitTest ("Theme", "UiTheme") {}

    static int accentPixels (juce::Component& c)
    {
        const auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        int n = 0;
        for (int y = 0; y < img.getHeight(); ++y)
            for (int x = 0; x < img.getWidth(); ++x)
            {
                const auto p = img.getPixelAt (x, y);
                if (std::abs (p.getRed() - 0xf0) + std::abs (p.getGreen() - 0xa0) + std::abs (p.getBlue() - 0x50) <= 30
                    && p.getAlpha() > 200)
                    ++n;
            }
        return n;
    }

    static bool near (juce::Colour a, juce::Colour b)
    {
        return std::abs (a.getRed() - b.getRed()) + std::abs (a.getGreen() - b.getGreen())
             + std::abs (a.getBlue() - b.getBlue()) <= 12;
    }

    void runTest() override
    {
        beginTest ("colours are the theme's exact values");
        {
            expect (ui::colours::bg     == juce::Colour (0xff16181b));
            expect (ui::colours::panel  == juce::Colour (0xff1f2226));
            expect (ui::colours::well   == juce::Colour (0xff111316));
            expect (ui::colours::line   == juce::Colour (0xff2d3137));
            expect (ui::colours::text   == juce::Colour (0xffe6e9ed));
            expect (ui::colours::dim    == juce::Colour (0xff8a919a));
            expect (ui::colours::accent == juce::Colour (0xfff0a050));
            expect (ui::colours::level  == juce::Colour (0xff4ea4e6));
            expect (ui::colours::knob   == juce::Colour (0xff2a2e33));
        }

        beginTest ("rotary 0..100 (Mix): 0 draws no arc, 50 half, 100 the full arc");
        {
            ui::Theme theme;
            juce::Slider s (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
            s.setLookAndFeel (&theme);
            s.setRange (0.0, 100.0, 1.0);
            s.setSize (60, 60);
            s.setValue (0.0, juce::dontSendNotification);
            expectEquals (accentPixels (s), 0);
            s.setValue (50.0, juce::dontSendNotification);
            const int halfArc = accentPixels (s);
            s.setValue (100.0, juce::dontSendNotification);
            const int fullArc = accentPixels (s);
            expectGreaterThan (halfArc, 50);
            expectGreaterThan (fullArc, halfArc + 50);
            s.setLookAndFeel (nullptr);
        }

        beginTest ("toggle pill: on = accent with the knob right, off = line colour with the knob left");
        {
            ui::Theme theme;
            juce::ToggleButton t ("Bypass");
            t.setLookAndFeel (&theme);
            t.setSize (76, 20);
            t.setToggleState (true, juce::dontSendNotification);
            auto img = t.createComponentSnapshot (t.getLocalBounds(), true, 1.0f);
            expect (near (img.getPixelAt (5, 10), ui::colours::accent), "on: pill is accent");
            expect (near (img.getPixelAt (19, 10), juce::Colours::white), "on: knob at the right");
            t.setToggleState (false, juce::dontSendNotification);
            img = t.createComponentSnapshot (t.getLocalBounds(), true, 1.0f);
            expect (near (img.getPixelAt (20, 10), ui::colours::line), "off: pill is the line colour");
            expect (near (img.getPixelAt (7, 10), juce::Colours::white), "off: knob at the left");
            t.setLookAndFeel (nullptr);
        }
    }
};


static UiThemeTests uiThemeTests;

/** Minimal processor owning one 4-way and one 2-way choice parameter, so the selector tests need no plugin. */
struct KitProcessor final : juce::AudioProcessor
{
    KitProcessor()
        : juce::AudioProcessor (BusesProperties()),
          apvts (*this, nullptr, "KIT", makeLayout())
    {
    }

    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout l;
        l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "level", 1 }, "Level",
                                                             juce::StringArray { "6", "12", "18", "21" }, 1));
        l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "model", 1 }, "Model",
                                                             juce::StringArray { "Standard", "Light" }, 0));
        return l;
    }

    const juce::String getName() const override { return "Kit"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& d) override
    {
        if (auto xml = apvts.copyState().createXml())
            copyXmlToBinary (*xml, d);
    }
    void setStateInformation (const void* d, int n) override
    {
        if (auto xml = getXmlFromBinary (d, n))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }

    juce::AudioProcessorValueTreeState apvts;
};

constexpr const char* kLevelId = "level";
constexpr const char* kModelId = "model";

class UiSelectorTests final : public juce::UnitTest
{
public:
    UiSelectorTests() : juce::UnitTest ("Segmented selector", "UiSelector") {}

    static juce::RangedAudioParameter& param (KitProcessor& p, const char* id)
    {
        return *p.apvts.getParameter (id);
    }

    static void setIndex (KitProcessor& p, const char* id, int index)
    {
        auto& prm = param (p, id);
        prm.setValueNotifyingHost (prm.convertTo0to1 ((float) index));
    }

    static bool near (juce::Colour a, juce::Colour b)
    {
        return std::abs (a.getRed() - b.getRed()) + std::abs (a.getGreen() - b.getGreen())
             + std::abs (a.getBlue() - b.getBlue()) <= 12;
    }

    /** Colour inside pill `i`, 4 px from its left end at mid height (clear of the centred text). */
    static juce::Colour fillOf (const juce::Image& img, const ui::SegmentedSelector& s, int i)
    {
        const auto r = s.getSegmentBounds (i);
        return img.getPixelAt (r.getX() + 4, r.getCentreY());
    }

    /** Counts gestures and value changes of one parameter. */
    struct ChangeCounter final : juce::AudioProcessorParameter::Listener
    {
        void parameterValueChanged (int, float) override                { ++values; }
        void parameterGestureChanged (int, bool) override               { ++gestures; }
        int values = 0, gestures = 0;
    };

    static juce::MouseEvent mouseAt (juce::Component& c, juce::Point<int> pos,
                                     juce::ModifierKeys mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier))
    {
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), pos.toFloat(), mods,
                 juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                 juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
                 juce::MouseInputSource::defaultTiltY, &c, &c, now, pos.toFloat(), now, 1, false };
    }

    void runTest() override
    {
        beginTest ("2 x 2 grid of 29 x 20 pills, 4 px gaps (4-way); 1 column of 62 x 20 (2-way)");
        {
            KitProcessor p;
            ui::SegmentedSelector level (param (p, kLevelId), { "6", "12", "18", "21" }, 2, 29, 20, 4);
            expect (level.gridBounds() == juce::Rectangle<int> (0, 0, 62, 44));
            expect (level.getLocalBounds() == juce::Rectangle<int> (0, 0, 62, 44));
            expect (level.getSegmentBounds (0) == juce::Rectangle<int> (0, 0, 29, 20));
            expect (level.getSegmentBounds (1) == juce::Rectangle<int> (33, 0, 29, 20));
            expect (level.getSegmentBounds (2) == juce::Rectangle<int> (0, 24, 29, 20));
            expect (level.getSegmentBounds (3) == juce::Rectangle<int> (33, 24, 29, 20));
            expectEquals (level.segmentAt ({ 45, 34 }), 3);
            expectEquals (level.segmentAt ({ 31, 10 }), -1);   // horizontal gap
            expectEquals (level.segmentAt ({ 10, 22 }), -1);   // vertical gap

            ui::SegmentedSelector model (param (p, kModelId), { "Standard", "Light" }, 1, 62, 20, 4);
            expect (model.gridBounds() == juce::Rectangle<int> (0, 0, 62, 44));
            expect (model.getSegmentBounds (0) == juce::Rectangle<int> (0, 0, 62, 20));
            expect (model.getSegmentBounds (1) == juce::Rectangle<int> (0, 24, 62, 20));
        }

        beginTest ("selection follows the parameter: initial value, host automation, state load");
        {
            KitProcessor p;
            ui::SegmentedSelector level (param (p, kLevelId), { "6", "12", "18", "21" }, 2, 29, 20, 4);
            expectEquals (level.getSelectedIndex(), 1);             // default 12 dB
            setIndex (p, kLevelId, 3);
            expectEquals (level.getSelectedIndex(), 3);

            KitProcessor other;
            setIndex (other, kLevelId, 0);
            juce::MemoryBlock state;
            other.getStateInformation (state);
            p.setStateInformation (state.getData(), (int) state.getSize());
            expectEquals (level.getSelectedIndex(), 0);
        }

        beginTest ("a click on a pill sets the parameter; clicks on a gap or the selected pill change nothing");
        {
            KitProcessor p;
            ui::SegmentedSelector model (param (p, kModelId), { "Standard", "Light" }, 1, 62, 20, 4);
            model.mouseDown (mouseAt (model, model.getSegmentBounds (1).getCentre()));
            expectEquals (p.apvts.getRawParameterValue (kModelId)->load(), 1.0f);
            expectEquals (model.getSelectedIndex(), 1);
            model.mouseDown (mouseAt (model, { 30, 22 }));          // the gap between the pills
            expectEquals (p.apvts.getRawParameterValue (kModelId)->load(), 1.0f);
            model.selectIndex (0);
            expectEquals (p.apvts.getRawParameterValue (kModelId)->load(), 0.0f);
            model.selectIndex (7);                                  // out of range: ignored
            expectEquals (p.apvts.getRawParameterValue (kModelId)->load(), 0.0f);
        }

        beginTest ("re-clicking the selected pill sends no gesture and no value change; a right-click changes nothing");
        {
            KitProcessor p;
            auto& prm = param (p, kModelId);
            ui::SegmentedSelector model (prm, { "Standard", "Light" }, 1, 62, 20, 4);
            ChangeCounter counter;
            prm.addListener (&counter);

            model.mouseDown (mouseAt (model, model.getSegmentBounds (0).getCentre()));   // already selected
            expectEquals (counter.gestures, 0);
            expectEquals (counter.values, 0);

            model.mouseDown (mouseAt (model, model.getSegmentBounds (1).getCentre(),
                                      juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier)));
            expectEquals (counter.values, 0);
            expectEquals (p.apvts.getRawParameterValue (kModelId)->load(), 0.0f);

            model.mouseDown (mouseAt (model, model.getSegmentBounds (1).getCentre(),
                                      juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier)));
            expectEquals (p.apvts.getRawParameterValue (kModelId)->load(), 1.0f);
            expect (counter.values > 0);
            prm.removeListener (&counter);
        }

        beginTest ("Theme draws the selected pill accent-filled and the others knob-filled");
        {
            KitProcessor p;
            ui::Theme theme;
            ui::SegmentedSelector level (param (p, kLevelId), { "6", "12", "18", "21" }, 2, 29, 20, 4);
            level.setLookAndFeel (&theme);

            auto img = level.createComponentSnapshot (level.getLocalBounds(), true, 1.0f);
            expect (near (fillOf (img, level, 1), ui::colours::accent));
            for (int i : { 0, 2, 3 })
                expect (near (fillOf (img, level, i), ui::colours::knob), "pill " + juce::String (i));

            setIndex (p, kLevelId, 2);
            img = level.createComponentSnapshot (level.getLocalBounds(), true, 1.0f);
            expect (near (fillOf (img, level, 2), ui::colours::accent));
            expect (near (fillOf (img, level, 1), ui::colours::knob));
            level.setLookAndFeel (nullptr);
        }
    }
};


static UiSelectorTests uiSelectorTests;

class UiMeterTests final : public juce::UnitTest
{
public:
    UiMeterTests() : juce::UnitTest ("Meter ballistics and MeterView", "UiMeter") {}

    void runTest() override
    {
        constexpr float frame = 1.0f / 30.0f;   // the editor's 30 Hz timer

        beginTest ("ballistics start at the floor; attack is instant");
        {
            MeterBallistics b;
            expectEquals (b.barDb(), MeterBallistics::floorDb);
            expectEquals (b.holdDb(), MeterBallistics::floorDb);
            b.update (-6.0f, frame);
            expectEquals (b.barDb(), -6.0f);
            expectEquals (b.holdDb(), -6.0f);
            for (int i = 0; i < 10; ++i) b.update (-100.0f, frame);   // bar falling
            b.update (-3.0f, frame);                                    // a louder peak shows at once
            expectEquals (b.barDb(), -3.0f);
            expectEquals (b.holdDb(), -3.0f);
        }

        beginTest ("bar falls 12 dB/s: 1 s after a -10 dB peak it shows -22 dB");
        {
            MeterBallistics b;
            b.update (-10.0f, frame);
            for (int i = 0; i < 30; ++i) b.update (-100.0f, frame);
            expectWithinAbsoluteError (b.barDb(), -22.0f, 1.0e-3f);
        }

        beginTest ("hold tick holds 1.5 s, then falls 20 dB/s, never below the bar");
        {
            MeterBallistics b;
            b.update (-10.0f, frame);
            int i = 0;
            for (; i < 44; ++i) b.update (-100.0f, frame);     // 1.467 s: still holding
            expectEquals (b.holdDb(), -10.0f);
            for (; i < 75; ++i) b.update (-100.0f, frame);     // 2.5 s: 1.0 s of tick fall
            expectWithinAbsoluteError (b.holdDb(), -30.0f, 0.01f);
            expectWithinAbsoluteError (b.barDb(), -40.0f, 0.01f);
            for (; i < 90; ++i) b.update (-100.0f, frame);     // 3.0 s
            expectWithinAbsoluteError (b.holdDb(), -40.0f, 0.01f);
            expectWithinAbsoluteError (b.barDb(), -46.0f, 0.01f);
            for (; i < 120; ++i) b.update (-100.0f, frame);    // 4.0 s: the tick caught the bar at 3.75 s
            expectWithinAbsoluteError (b.barDb(), -58.0f, 0.01f);
            expectWithinAbsoluteError (b.holdDb(), b.barDb(), 1.0e-4f);
        }

        beginTest ("floor clamp: nothing below -60 dB, silence settles exactly on the floor");
        {
            MeterBallistics b;
            b.update (-200.0f, frame);
            expectEquals (b.barDb(), MeterBallistics::floorDb);
            expectEquals (b.holdDb(), MeterBallistics::floorDb);
            b.update (std::numeric_limits<float>::quiet_NaN(), frame);
            expectEquals (b.barDb(), MeterBallistics::floorDb);
            b.update (-10.0f, frame);
            for (int i = 0; i < 300; ++i) b.update (-100.0f, frame);   // 10 s
            expectEquals (b.barDb(), MeterBallistics::floorDb);
            expectEquals (b.holdDb(), MeterBallistics::floorDb);
            b.update (-5.0f, frame);
            b.reset();
            expectEquals (b.barDb(), MeterBallistics::floorDb);
            expectEquals (b.holdDb(), MeterBallistics::floorDb);
        }

        beginTest ("MeterView three rows height");
        {
            expectEquals (ui::MeterView::kHeight (3), 3 * 16 + 2 * 5);
            expectEquals (ui::MeterView::kHeight (2), 2 * 16 + 5);
            PeakHold a, b, c;
            ui::MeterView mv ({ { "A", &a }, { "B", &b }, { "C", &c } });
            mv.setSize (247, ui::MeterView::kHeight (3));
            const float half = 0.5f;
            c.push (&half, 1);
            mv.tick (frame);
            expectEquals (mv.numberText (0), juce::String ("-inf"));
            expectEquals (mv.numberText (2), juce::String ("-6.0"));
        }

        beginTest ("MeterView: silence from the start never repaints");
        {
            PeakHold in, out;
            ui::MeterView mv ({ { "IN", &in }, { "OUT", &out } });
            mv.setSize (247, ui::MeterView::kHeight (2));
            for (int i = 0; i < 10; ++i) mv.tick (frame);
            expectEquals (mv.getRepaintRequestCount(), 0);
            expectEquals (mv.numberText (0), juce::String ("-inf"));
            expectEquals (mv.numberText (1), juce::String ("-inf"));
        }

        beginTest ("MeterView: a peak repaints; sub-pixel changes and a settled meter do not");
        {
            PeakHold in, out;
            ui::MeterView mv ({ { "IN", &in }, { "OUT", &out } });
            mv.setSize (247, ui::MeterView::kHeight (2));
            const float half = 0.5f;                       // -6.02 dBFS
            in.push (&half, 1);
            mv.tick (frame);
            expectEquals (mv.getRepaintRequestCount(), 1);
            expectEquals (mv.numberText (0), juce::String ("-6.0"));
            expectEquals (mv.numberText (1), juce::String ("-inf"));
            expectEquals (mv.ballistics (0).barDb(), 20.0f * std::log10 (0.5f));

            mv.tick (1.0e-4f);                             // bar moves 0.0012 dB: same pixel, same text
            expectEquals (mv.getRepaintRequestCount(), 1);

            for (int i = 0; i < 400; ++i) mv.tick (frame); // 13 s: bar and tick back on the floor
            expectGreaterThan (mv.getRepaintRequestCount(), 10);
            expectEquals (mv.numberText (0), juce::String ("-inf"));
            const int settled = mv.getRepaintRequestCount();
            mv.tick (frame);
            mv.tick (frame);
            expectEquals (mv.getRepaintRequestCount(), settled);
        }
    }
};


static UiMeterTests uiMeterTests;

class UiHelpTests final : public juce::UnitTest
{
public:
    UiHelpTests() : juce::UnitTest ("Help text", "UiHelp") {}

    void runTest() override
    {
        beginTest ("help has every tuning param plus nsLevel and monitorNoise");
        {
            const char* tuning[] = { "lfSuppression", "lfTransparency", "hfSuppression", "hfTransparency", "lfAttack", "lfDecay",
                                     "hfDucking", "hfEchoThreshold", "dtLfSuppression", "dtLfTransparency", "dtHfSuppression",
                                     "dtHfTransparency", "dtLfAttack", "dtLfDecay", "latencyComp", "roomReverb", "boundedErl",
                                     "clockDrift" };
            for (auto* id : tuning)
                expect (help::find (id) != nullptr, juce::String ("missing help for ") + id);
            expect (help::find ("nsLevel") != nullptr);
            expect (help::find ("monitorNoise") != nullptr);
            expect (help::find ("nope") == nullptr);
            expectEquals ((int) help::all().size(), 20);
        }
    }
};

static UiHelpTests uiHelpTests;
} // namespace aec
