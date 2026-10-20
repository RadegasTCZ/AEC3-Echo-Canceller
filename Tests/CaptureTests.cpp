#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/WasapiLoopbackCapture.h"

namespace
{
    /** No WASAPI, no thread: counts thread starts, the "thread" never runs (as if it died at once),
        and the recovery clock is set by the test. */
    struct FakeCapture final : public WasapiLoopbackCapture
    {
        int starts = 0;
        double nowSeconds = 100.0;

        void advanceMs (double ms) { nowSeconds += ms / 1000.0; }

    protected:
        void startCaptureThread() override { ++starts; }
        bool isCaptureThreadRunning() const override { return false; }
        int64_t recoveryNowTicks() const override { return juce::Time::secondsToHighResolutionTicks (nowSeconds); }
    };

    struct CaptureTests final : public juce::UnitTest
    {
        CaptureTests() : juce::UnitTest ("Capture", "Capture") {}

        void runRecoveryTests()
        {
            beginTest ("recovery: a dead capture thread is restarted once the backoff has elapsed");
            {
                FakeCapture cap;
                cap.startCapture();
                expectEquals (cap.starts, 1);
                expect (cap.getStatus() == WasapiLoopbackCapture::Status::Connecting);

                cap.checkHealthAndRecover();          // first attempt: nothing before it, so no wait
                expectEquals (cap.starts, 2);
                cap.checkHealthAndRecover();          // same instant: inside the backoff
                expectEquals (cap.starts, 2);
                expect (cap.getStatus() == WasapiLoopbackCapture::Status::Connecting);
            }

            beginTest ("recovery: backoff doubles between attempts up to the cap");
            {
                FakeCapture cap;
                cap.startCapture();
                cap.checkHealthAndRecover();          // attempt 1; next backoff 1000 ms
                expectEquals (cap.starts, 2);

                juce::StringArray seen;
                int expectedBackoff = 1000;
                for (int attempt = 0; attempt < 7; ++attempt)
                {
                    const int before = cap.starts;
                    cap.advanceMs (expectedBackoff - 10.0);
                    cap.checkHealthAndRecover();
                    expectEquals (cap.starts, before, "restarted before " + juce::String (expectedBackoff) + " ms");
                    cap.advanceMs (20.0);
                    cap.checkHealthAndRecover();
                    expectEquals (cap.starts, before + 1, "not restarted after " + juce::String (expectedBackoff) + " ms");
                    seen.add (juce::String (expectedBackoff));
                    expectedBackoff = juce::jmin (expectedBackoff * 2, 10000);
                }
                logMessage ("recovery backoffs (ms): " + seen.joinIntoString (", "));
            }

            beginTest ("recovery: no restart after stopCapture, even if the dying thread left Connecting");
            {
                FakeCapture cap;
                cap.startCapture();
                cap.checkHealthAndRecover();
                const int before = cap.starts;

                cap.stopCapture();
                // run()'s error path stores Connecting during its backoff; a thread stopped there
                // leaves it behind after stopCapture's Disconnected store raced with it
                cap.setStatusForTesting (WasapiLoopbackCapture::Status::Connecting);
                for (int i = 0; i < 50; ++i)
                {
                    cap.advanceMs (20000.0);
                    cap.checkHealthAndRecover();
                }
                expectEquals (cap.starts, before);

                cap.startCapture();                   // a later prepare starts it again
                expectEquals (cap.starts, before + 1);
            }
        }

        void runTest() override
        {
            runRecoveryTests();

            beginTest ("capture watchdog runs with no editor");
            {
                AEC3EchoCancellerAudioProcessor p;
                expect (p.isCaptureWatchdogRunning());
            }

            beginTest ("requestFlush discards queued samples on the reader side");
            {
                WasapiLoopbackCapture cap;
                std::vector<int16_t> data (1000, 123);
                expectEquals (cap.writeForTesting (data.data(), 1000), 1000);
                cap.requestFlush();

                std::vector<int16_t> out (2000, 0);
                expectEquals (cap.readSamples (out.data(), 2000), 0);

                expectEquals (cap.writeForTesting (data.data(), 10), 10);
                expectEquals (cap.readSamples (out.data(), 2000), 10);
                expectEquals ((int) out[0], 123);
            }

            beginTest ("destroying a processor with the watchdog running is clean");
            {
                for (int i = 0; i < 20; ++i)
                {
                    AEC3EchoCancellerAudioProcessor p;
                    p.startCaptureOnPrepare = false;
                    p.prepareToPlay (48000.0, 480);

                    juce::AudioBuffer<float> buf (2, 480);
                    buf.clear();
                    juce::MidiBuffer midi;
                    p.processBlock (buf, midi);

                    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
                    expect (p.isCaptureWatchdogRunning());
                }
                expect (true);
            }
        }
    };

    static CaptureTests captureTests;
}
