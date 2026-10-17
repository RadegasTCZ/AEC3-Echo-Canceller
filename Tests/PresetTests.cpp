#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PresetIO.h"

namespace
{
    float realValue (juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
    {
        return apvts.getParameterRange (id).convertFrom0to1 (apvts.getParameter (id)->getValue());
    }

    void setReal (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float v)
    {
        apvts.getParameter (id)->setValueNotifyingHost (apvts.getParameterRange (id).convertTo0to1 (v));
    }

    juce::File tempPreset (const juce::String& name)
    {
        return juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("aec3_preset_test_" + name + ".txt");
    }

    // Exactly what v5.0.0 PresetIO::savePreset wrote (no nsLevel).
    const char* const v5File =
        "[AEC3Preset]\n"
        "version=1\n"
        "name=old\n"
        "\n"
        "[Suppression]\n"
        "lfSuppression=40.0000\n"
        "lfTransparency=55.0000\n"
        "hfSuppression=60.0000\n"
        "hfTransparency=65.0000\n"
        "lfAttack=15.0000\n"
        "lfDecay=45.0000\n"
        "hfDucking=20.0000\n"
        "hfEchoThreshold=70.0000\n"
        "\n"
        "[DoubleTalk]\n"
        "dtLfSuppression=10.0000\n"
        "dtLfTransparency=15.0000\n"
        "dtHfSuppression=20.0000\n"
        "dtHfTransparency=90.0000\n"
        "dtLfAttack=50.0000\n"
        "dtLfDecay=5.0000\n"
        "\n"
        "[System]\n"
        "latencyComp=35.0000\n"
        "roomReverb=22.0000\n"
        "boundedErl=true\n"
        "clockDrift=true";
}

class PresetTests final : public juce::UnitTest
{
public:
    PresetTests() : juce::UnitTest ("Preset", "Preset") {}

    void runTest() override
    {
        beginTest ("v6 round trip");
        {
            AEC3EchoCancellerAudioProcessor a, b;
            auto& pa = a.getAPVTS();
            auto& pb = b.getAPVTS();
            setReal (pa, "lfSuppression", 40.0f);
            setReal (pa, "roomReverb", 22.0f);
            setReal (pa, "boundedErl", 1.0f);
            setReal (pa, "nsLevel", 3.0f);

            auto f = tempPreset ("roundtrip");
            juce::String err;
            expect (PresetIO::savePreset (f, pa, err), err);
            expect (PresetIO::loadPreset (f, pb, err), err);
            expectEquals (realValue (pb, "lfSuppression"), 40.0f);
            expectEquals (realValue (pb, "roomReverb"), 22.0f);
            expectEquals (realValue (pb, "boundedErl"), 1.0f);
            expectEquals (realValue (pb, "nsLevel"), 3.0f);
            f.deleteFile();
        }

        beginTest ("v5 file without nsLevel loads with nsLevel 12 dB");
        {
            AEC3EchoCancellerAudioProcessor p;
            auto& apvts = p.getAPVTS();
            setReal (apvts, "nsLevel", 4.0f);

            auto f = tempPreset ("v5");
            f.replaceWithText (v5File);
            juce::String err;
            expect (PresetIO::loadPreset (f, apvts, err), err);
            expectEquals (realValue (apvts, "nsLevel"), 2.0f);
            expectEquals (realValue (apvts, "lfSuppression"), 40.0f);
            expectEquals (realValue (apvts, "dtLfDecay"), 5.0f);
            expectEquals (realValue (apvts, "clockDrift"), 1.0f);
            f.deleteFile();
        }

        beginTest ("missing and unparseable keys take defaults");
        {
            AEC3EchoCancellerAudioProcessor p;
            auto& apvts = p.getAPVTS();
            setReal (apvts, "lfDecay", 80.0f);
            setReal (apvts, "hfDucking", 60.0f);

            auto f = tempPreset ("defaults");
            f.replaceWithText ("[AEC3Preset]\nversion=1\n[Suppression]\nlfSuppression=50\nlfDecay=abc\n");
            juce::String err;
            expect (PresetIO::loadPreset (f, apvts, err), err);
            expectEquals (realValue (apvts, "lfSuppression"), 50.0f);
            expectEquals (realValue (apvts, "lfDecay"), 25.0f);
            expectEquals (realValue (apvts, "hfDucking"), 0.0f);
            f.deleteFile();
        }

        beginTest ("out-of-range clamped");
        {
            AEC3EchoCancellerAudioProcessor p;
            auto& apvts = p.getAPVTS();
            auto f = tempPreset ("clamp");
            f.replaceWithText ("[AEC3Preset]\n[System]\nroomReverb=2\nlatencyComp=9999\n");
            juce::String err;
            expect (PresetIO::loadPreset (f, apvts, err), err);
            expectEquals (realValue (apvts, "roomReverb"), 13.0f);
            expectEquals (realValue (apvts, "latencyComp"), 200.0f);
            f.deleteFile();
        }

        beginTest ("non-preset file rejected, state untouched");
        {
            AEC3EchoCancellerAudioProcessor p;
            auto& apvts = p.getAPVTS();
            setReal (apvts, "lfSuppression", 40.0f);
            setReal (apvts, "nsLevel", 4.0f);

            auto f = tempPreset ("hello");
            f.replaceWithText ("hello");
            juce::String err;
            expect (! PresetIO::loadPreset (f, apvts, err));
            expect (err.isNotEmpty());
            expectEquals (realValue (apvts, "lfSuppression"), 40.0f);
            expectEquals (realValue (apvts, "nsLevel"), 4.0f);

            err.clear();
            f.replaceWithText ("[AEC3Preset]\nversion=1\nfoo=bar\n");
            expect (! PresetIO::loadPreset (f, apvts, err));
            expect (err.isNotEmpty());
            expectEquals (realValue (apvts, "lfSuppression"), 40.0f);
            f.deleteFile();
        }

        beginTest ("preset file over 64 KB rejected, state untouched; just under loads");
        {
            AEC3EchoCancellerAudioProcessor p;
            auto& apvts = p.getAPVTS();
            setReal (apvts, "lfSuppression", 30.0f);

            // A valid preset padded with comment lines
            auto padded = [] (int targetBytes)
            {
                juce::String s (v5File);
                s << "\n";
                while (s.getNumBytesAsUTF8() + 64 <= (size_t) targetBytes)
                    s << ";" << juce::String::repeatedString ("x", 62) << "\n";
                while ((int) s.getNumBytesAsUTF8() < targetBytes)
                    s << "\n";
                return s;
            };

            auto f = tempPreset ("big");
            expect (f.replaceWithText (padded (64 * 1024 + 1), false, false, "\n"));
            expectEquals ((int) f.getSize(), 64 * 1024 + 1);
            juce::String err;
            expect (! PresetIO::loadPreset (f, apvts, err));
            expect (err.isNotEmpty());
            expectWithinAbsoluteError (realValue (apvts, "lfSuppression"), 30.0f, 1.0e-3f);

            err.clear();
            expect (f.replaceWithText (padded (64 * 1024), false, false, "\n"));
            expectEquals ((int) f.getSize(), 64 * 1024);
            expect (PresetIO::loadPreset (f, apvts, err), err);
            expectEquals (realValue (apvts, "lfSuppression"), 40.0f);
            f.deleteFile();
        }

        beginTest ("preset load wraps every parameter change in a change gesture");
        {
            AEC3EchoCancellerAudioProcessor p;
            auto& apvts = p.getAPVTS();

            struct GestureCounter final : juce::AudioProcessorListener
            {
                int begins = 0, ends = 0, changesOutsideGesture = 0, open = 0;
                void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override
                {
                    if (open == 0)
                        ++changesOutsideGesture;
                }
                void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
                void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int) override { ++begins; ++open; }
                void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int) override   { ++ends; --open; }
            } counter;

            auto f = tempPreset ("gesture");
            f.replaceWithText (v5File);
            p.addListener (&counter);
            juce::String err;
            expect (PresetIO::loadPreset (f, apvts, err), err);
            p.removeListener (&counter);
            f.deleteFile();

            logMessage ("preset load: gesture begins " + juce::String (counter.begins) + ", ends " + juce::String (counter.ends)
                        + ", changes outside a gesture " + juce::String (counter.changesOutsideGesture));
            expectEquals (counter.begins, PresetIO::kPresetParamIds.size());
            expectEquals (counter.ends, counter.begins);
            expectEquals (counter.changesOutsideGesture, 0);
        }
    }
};

static PresetTests presetTests;
