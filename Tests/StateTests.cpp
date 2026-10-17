#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/ParamIds.h"

namespace
{
    struct Entry { const char* id; float value; };

    // The 20 parameters of a v4.1.0 / v5 session, all at non-default values.
    const Entry v41Values[] = {
        { "aecEnabled", 0.0f }, { "monitorCapture", 1.0f },
        { "lfSuppression", 40.0f }, { "lfTransparency", 55.0f }, { "hfSuppression", 60.0f },
        { "hfTransparency", 65.0f }, { "lfAttack", 15.0f }, { "lfDecay", 45.0f },
        { "hfDucking", 20.0f }, { "hfEchoThreshold", 70.0f },
        { "dtLfSuppression", 10.0f }, { "dtLfTransparency", 15.0f }, { "dtHfSuppression", 20.0f },
        { "dtHfTransparency", 90.0f }, { "dtLfAttack", 50.0f }, { "dtLfDecay", 5.0f },
        { "latencyComp", 35.0f }, { "roomReverb", 22.0f }, { "boundedErl", 1.0f }, { "clockDrift", 1.0f }
    };

    juce::String chunkXml (const juce::String& deviceId, const juce::String& uiMode,
                           const juce::StringArray& skipIds = {},
                           const juce::String& overrideId = {}, float overrideValue = 0.0f)
    {
        juce::String xml = "<AEC3 captureDeviceId=\"" + deviceId + "\"";

        if (uiMode.isNotEmpty())
            xml << " uiMode=\"" << uiMode << "\"";

        xml << ">";

        for (auto& e : v41Values)
        {
            if (skipIds.contains (e.id))
                continue;

            const float v = overrideId == e.id ? overrideValue : e.value;
            xml << "<PARAM id=\"" << e.id << "\" value=\"" << juce::String (v, 1) << "\"/>";
        }

        xml << "</AEC3>";
        return xml;
    }

    juce::MemoryBlock binaryFromXml (const juce::String& xmlText)
    {
        juce::MemoryBlock block;

        if (auto xml = juce::parseXML (xmlText))
            juce::AudioProcessor::copyXmlToBinary (*xml, block);

        return block;
    }

    void load (AEC3EchoCancellerAudioProcessor& p, const juce::MemoryBlock& block)
    {
        p.setStateInformation (block.getData(), (int) block.getSize());
    }

    void setRaw (AEC3EchoCancellerAudioProcessor& p, const char* id, float v)
    {
        auto* prm = p.getAPVTS().getParameter (id);
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    }

    float raw (AEC3EchoCancellerAudioProcessor& p, const char* id)
    {
        return p.getAPVTS().getRawParameterValue (id)->load();
    }

    const char* const allIds[] = {
        "aecEnabled", "monitorCapture", "lfSuppression", "lfTransparency", "hfSuppression",
        "hfTransparency", "lfAttack", "lfDecay", "hfDucking", "hfEchoThreshold",
        "dtLfSuppression", "dtLfTransparency", "dtHfSuppression", "dtHfTransparency",
        "dtLfAttack", "dtLfDecay", "latencyComp", "roomReverb", "boundedErl", "clockDrift",
        "nsLevel", "monitorNoise" };
}

class StateTests final : public juce::UnitTest
{
public:
    StateTests() : juce::UnitTest ("State", "State") {}

    void runTest() override
    {
        beginTest ("parameter id constants match the registered parameters");
        {
            AEC3EchoCancellerAudioProcessor p;
            expectEquals ((int) p.getParameters().size(), 22);

            for (auto* id : allIds)
                expect (p.getAPVTS().getParameter (id) != nullptr, juce::String (id));

            expectEquals (juce::String (aec::ids::nsLevel), juce::String ("nsLevel"));
            expectEquals (juce::String (aec::ids::monitorNoise), juce::String ("monitorNoise"));
        }

        beginTest ("Normal defaults");
        {
            AEC3EchoCancellerAudioProcessor p;
            expectEquals (raw (p, "lfAttack"), 35.0f);
            expectEquals (raw (p, "hfEchoThreshold"), 55.0f);
            expectEquals (raw (p, "dtLfAttack"), 35.0f);
            expectEquals (raw (p, "nsLevel"), 2.0f);
            expectEquals (raw (p, "monitorNoise"), 0.0f);
        }

        beginTest ("v4.1.0 chunk restores every saved value");
        {
            AEC3EchoCancellerAudioProcessor p;
            load (p, binaryFromXml (chunkXml ("X", "")));

            for (auto& e : v41Values)
                expectEquals (raw (p, e.id), e.value, juce::String (e.id));

            expectEquals (p.getLoopbackCapture().getDeviceId(), juce::String ("X"));
        }

        beginTest ("missing nsLevel becomes 12 dB even if the instance had Off");
        {
            AEC3EchoCancellerAudioProcessor p;
            setRaw (p, "nsLevel", 0.0f);
            expectEquals (raw (p, "nsLevel"), 0.0f);
            load (p, binaryFromXml (chunkXml ("X", "")));
            expectEquals (raw (p, "nsLevel"), 2.0f);
        }

        beginTest ("missing monitorNoise becomes false");
        {
            AEC3EchoCancellerAudioProcessor p;
            setRaw (p, "monitorNoise", 1.0f);
            load (p, binaryFromXml (chunkXml ("X", "")));
            expectEquals (raw (p, "monitorNoise"), 0.0f);
        }

        beginTest ("missing tuning param takes its default");
        {
            AEC3EchoCancellerAudioProcessor p;
            setRaw (p, "lfAttack", 80.0f);
            load (p, binaryFromXml (chunkXml ("X", "", { "lfAttack" })));
            expectEquals (raw (p, "lfAttack"), 35.0f);
            expectEquals (raw (p, "lfSuppression"), 40.0f);
        }

        beginTest ("out-of-range values are clamped");
        {
            AEC3EchoCancellerAudioProcessor a, b, c;
            load (a, binaryFromXml (chunkXml ("X", "", {}, "roomReverb", 5.0f)));
            expectEquals (raw (a, "roomReverb"), 13.0f);
            load (b, binaryFromXml (chunkXml ("X", "", {}, "latencyComp", 500.0f)));
            expectEquals (raw (b, "latencyComp"), 200.0f);
            load (c, binaryFromXml (chunkXml ("X", "", {}, "lfSuppression", 140.0f)));
            expectEquals (raw (c, "lfSuppression"), 100.0f);
        }

        beginTest ("non-finite values in host state take their defaults; prepare + process stay finite");
        {
            // A tuning parameter, roomReverb (filter length), latencyComp and nsLevel as "nan"
            auto xmlText = chunkXml ("X", "");
            for (auto* id : { "lfSuppression", "roomReverb", "latencyComp" })
            {
                const auto before = "id=\"" + juce::String (id) + "\" value=\"";
                const int start = xmlText.indexOf (before) + before.length();
                const int end = xmlText.indexOfChar (start, '"');
                xmlText = xmlText.replaceSection (start, end - start, "nan");
            }
            xmlText = xmlText.replace ("</AEC3>", "<PARAM id=\"nsLevel\" value=\"nan\"/></AEC3>");
            expect (xmlText.contains ("id=\"roomReverb\" value=\"nan\""));

            AEC3EchoCancellerAudioProcessor p;
            p.startCaptureOnPrepare = false;   // no WASAPI in tests
            load (p, binaryFromXml (xmlText));

            for (auto* prm : p.getParameters())
                expect (std::isfinite (prm->getValue()), prm->getName (64));
            for (auto* id : allIds)
                expect (std::isfinite (raw (p, id)), juce::String (id));

            expectEquals (raw (p, "lfSuppression"), 75.0f);
            expectEquals (raw (p, "roomReverb"), 13.0f);
            expectEquals (raw (p, "latencyComp"), 0.0f);
            expectEquals (raw (p, "nsLevel"), 2.0f);
            expectEquals (raw (p, "lfTransparency"), 55.0f);   // the finite values are still restored

            setRaw (p, "aecEnabled", 1.0f);
            p.prepareToPlay (48000.0, 480);

            juce::AudioBuffer<float> buf (2, 480);
            juce::MidiBuffer midi;
            juce::Random rng (5);
            bool finite = true;
            for (int b = 0; b < 300; ++b)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 480; ++i)
                        buf.setSample (ch, i, 0.1f * (rng.nextFloat() * 2.0f - 1.0f));
                p.processBlock (buf, midi);
                for (int i = 0; i < 480; ++i)
                    finite = finite && std::isfinite (buf.getSample (0, i));
            }
            expect (finite, "output finite");
            p.releaseResources();
        }

        beginTest ("v5 uiMode advanced opens Advanced; junk falls back to main");
        {
            AEC3EchoCancellerAudioProcessor p;
            expect (! p.isAdvancedOpen());
            load (p, binaryFromXml (chunkXml ("X", "advanced")));
            expect (p.isAdvancedOpen());
            load (p, binaryFromXml (chunkXml ("X", "banana")));
            expect (! p.isAdvancedOpen());
            load (p, binaryFromXml (chunkXml ("X", "advanced")));
            load (p, binaryFromXml (chunkXml ("X", "basic")));
            expect (! p.isAdvancedOpen());
            load (p, binaryFromXml (chunkXml ("X", "advanced")));
            load (p, binaryFromXml (chunkXml ("X", "")));
            expect (! p.isAdvancedOpen());
        }

        beginTest ("garbage bytes leave state unchanged");
        {
            AEC3EchoCancellerAudioProcessor p;
            load (p, binaryFromXml (chunkXml ("X", "advanced")));

            float before[22];
            for (int i = 0; i < 22; ++i)
                before[i] = raw (p, allIds[i]);

            juce::Random rng (1234);
            unsigned char bytes[64];
            for (auto& b : bytes)
                b = (unsigned char) rng.nextInt (256);

            p.setStateInformation (bytes, (int) sizeof (bytes));

            for (int i = 0; i < 22; ++i)
                expectEquals (raw (p, allIds[i]), before[i], juce::String (allIds[i]));

            expect (p.isAdvancedOpen());
            expectEquals (p.getLoopbackCapture().getDeviceId(), juce::String ("X"));
        }

        beginTest ("round trip");
        {
            AEC3EchoCancellerAudioProcessor a;
            load (a, binaryFromXml (chunkXml ("DEV", "advanced")));
            setRaw (a, "nsLevel", 4.0f);
            setRaw (a, "monitorNoise", 1.0f);

            juce::MemoryBlock state;
            a.getStateInformation (state);

            AEC3EchoCancellerAudioProcessor b;
            load (b, state);

            for (auto* id : allIds)
                expectEquals (raw (b, id), raw (a, id), juce::String (id));

            expectEquals (raw (b, "nsLevel"), 4.0f);
            expectEquals (raw (b, "monitorNoise"), 1.0f);
            expect (b.isAdvancedOpen());
            expectEquals (b.getLoopbackCapture().getDeviceId(), juce::String ("DEV"));
        }
    }
};

static StateTests stateTests;
