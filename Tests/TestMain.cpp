#include <JuceHeader.h>
#include <cstdio>

namespace
{
    /** UnitTestRunner::logMessage() goes to Logger::writeToLog(), which on Windows only reaches
        OutputDebugString. Mirror it to stdout so results are visible in the console. */
    struct StdoutLogger final : public juce::Logger
    {
        void logMessage (const juce::String& message) override
        {
            std::printf ("%s\n", message.toRawUTF8());
        }
    };

    void printUsage()
    {
        std::printf ("usage: AEC3Tests [--category <name>]\n");
    }
}

int main (int argc, char* argv[])
{
    std::setvbuf (stdout, nullptr, _IONBF, 0); // a crash must not swallow buffered output

    juce::String category;

    for (int i = 1; i < argc; ++i)
    {
        const auto arg = juce::String::fromUTF8 (argv[i]);

        if (arg == "--category" && i + 1 < argc)
        {
            category = juce::String::fromUTF8 (argv[++i]);
        }
        else
        {
            printUsage();
            return 2;
        }
    }

    // Processor / editor tests need the message manager (APVTS, Timer, Component).
    juce::ScopedJuceInitialiser_GUI gui;

    if (category.isNotEmpty() && ! juce::UnitTest::getAllCategories().contains (category))
    {
        std::printf ("unknown category: %s\navailable: %s\n",
                     category.toRawUTF8(),
                     juce::UnitTest::getAllCategories().joinIntoString (", ").toRawUTF8());
        return 2;
    }

    StdoutLogger logger;
    juce::Logger::setCurrentLogger (&logger);

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);

    if (category.isNotEmpty())
        runner.runTestsInCategory (category);
    else
        runner.runAllTests();

    int passes = 0, failures = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        passes   += runner.getResult (i)->passes;
        failures += runner.getResult (i)->failures;
    }

    juce::Logger::setCurrentLogger (nullptr);

    if (runner.getNumResults() == 0)
    {
        std::printf ("\nFAIL: no tests ran\n");
        return 1;
    }

    std::printf ("\n%s: %d check(s), %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", passes + failures, failures);
    return failures == 0 ? 0 : 1;
}
