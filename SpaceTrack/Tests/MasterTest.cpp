// Headless check of SpaceMaster's "All stereo" / "All 3D" buttons. Not part of the plugins.
// Build with -DSPACETRACK_TESTS=ON, run: SpaceMasterTest <folder for window pictures>
// It has its own private shared memory (see CMakeLists.txt), so it cannot touch a running FL Studio.
#include <JuceHeader.h>
#include "../Source/MasterProcessor.h"
#include "../Source/MasterEditor.h"
#include "../Source/SharedRegistry.h"

static int failures = 0;

static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
    if (! ok) ++failures;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir = argc > 1 ? juce::File (juce::String (argv[1])) : juce::File::getCurrentWorkingDirectory();
    outDir.createDirectory();

    auto* reg = getSharedRegistry();
    check (reg != nullptr, "shared memory available");
    if (reg == nullptr) return 1;

    // Four pretend tracks, in mixed modes, with a dead one that must be left alone
    const auto now = juce::Time::currentTimeMillis();
    const char* names[4] = { "Kick", "Vocal", "Pad", "Lead" };
    for (int i = 0; i < 4; ++i)
    {
        auto& s = reg->slots[i];
        s.owner.store (100u + (uint64_t) i);
        s.heartbeat.store (now);
        s.x.store (-0.6f + 0.4f * (float) i);
        s.y.store (0.5f - 0.3f * (float) i);
        s.z.store (0.2f * (float) i);
        s.mode3d.store (i % 2 == 0 ? 0u : 1u);
        juce::String (names[i]).copyToUTF8 (s.name, sizeof (s.name));
    }
    auto& dead = reg->slots[10];
    dead.owner.store (999u);
    dead.heartbeat.store (now - 60000);
    const auto deadSeq = dead.modeSeq.load();

    SpaceMasterProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* master = dynamic_cast<SpaceMasterEditor*> (ed.get());
    check (master != nullptr, "SpaceMaster window opens");
    if (master == nullptr) return 1;

    const auto snap = [&] (const juce::String& name)
    {
        const auto img = ed->createComponentSnapshot (ed->getLocalBounds());
        juce::FileOutputStream out (outDir.getChildFile (name + ".png"));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (img, out);
        check (img.isValid() && out.getStatus().wasOk(), "window picture " + name);
    };

    master->refreshAllButtons();
    snap ("master-mixed");

    for (bool want3D : { true, false })
    {
        uint32_t seqBefore[4];
        for (int i = 0; i < 4; ++i) seqBefore[(size_t) i] = reg->slots[i].modeSeq.load();

        master->setAllTracksMode (want3D);

        bool allSent = true;
        for (int i = 0; i < 4; ++i)
            allSent = allSent && reg->slots[i].modeSeq.load() != seqBefore[(size_t) i]
                              && reg->slots[i].cmdMode.load() == (want3D ? 1u : 0u);
        check (allSent, juce::String (want3D ? "All 3D" : "All stereo") + " reaches every live track");
        check (proc.view3D == want3D, "the field view follows");

        // pretend the tracks obeyed, as their timers would
        for (int i = 0; i < 4; ++i) reg->slots[i].mode3d.store (want3D ? 1u : 0u);
        master->refreshAllButtons();
        snap (want3D ? "master-all-3d" : "master-all-stereo");
    }

    check (dead.modeSeq.load() == deadSeq, "a dead slot gets no command");

    proc.roomSize = 0.8f;
    proc.roomAmount = 0.6f;
    proc.publishRoom();
    check (std::abs (reg->roomSize.load() - 0.8f) < 0.001f && std::abs (reg->roomAmount.load() - 0.6f) < 0.001f
               && juce::Time::currentTimeMillis() - reg->roomBeat.load() < 2000, "room settings reach the tracks' shared memory");

    juce::MemoryBlock state;
    proc.getStateInformation (state);
    SpaceMasterProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());
    check (std::abs (restored.roomSize - 0.8f) < 0.001f && std::abs (restored.roomAmount - 0.6f) < 0.001f, "room settings are saved with the project");

    SpaceMasterProcessor oldProject;
    juce::MemoryBlock oldState;
    {
        juce::MemoryOutputStream out (oldState, false);
        out.writeBool (true);
        out.writeFloat (0.5f);
    }
    oldProject.setStateInformation (oldState.getData(), (int) oldState.getSize());
    check (oldProject.view3D && std::abs (oldProject.roomAmount - 0.25f) < 0.001f, "projects saved before the room still load");

    ed.reset();
    for (int i : { 0, 1, 2, 3, 10 })
        reg->slots[i].owner.store (0);

    std::cout << (failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
