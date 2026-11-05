#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SharedRegistry.h"
#include "HrtfData.h"
#include <cstring>

namespace
{
    // Unit vector for a position. The very centre counts as straight ahead.
    void directionOf (float x, float y, float z, float& ux, float& uy, float& uz, float& dist)
    {
        dist = std::sqrt (x * x + y * y + z * z);
        if (dist > 1.0e-4f) { ux = x / dist; uy = y / dist; uz = z / dist; }
        else                { ux = 0.0f; uy = 1.0f; uz = 0.0f; }
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout SpaceTrackProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "x", 1 }, "Left/Right",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "y", 1 }, "Back/Front",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "z", 1 }, "Height",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "mode3d", 1 }, "3D Mode", false));
    return layout;
}

SpaceTrackProcessor::SpaceTrackProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    apvts.state.setProperty ("trackName", "Track", nullptr);

    myId = (uint64_t) juce::Random::getSystemRandom().nextInt64() | 1ull;
    claimSlot();
    startTimerHz (30);
}

SpaceTrackProcessor::~SpaceTrackProcessor()
{
    stopTimer();
    if (gestureOpen) endAllGestures();

    if (auto* reg = getSharedRegistry(); reg != nullptr && slotIndex >= 0)
    {
        uint64_t expected = myId;
        reg->slots[slotIndex].owner.compare_exchange_strong (expected, 0);
    }
}

juce::Colour SpaceTrackProcessor::getTrackColour() const
{
    const auto stored = (juce::uint32) (int) apvts.state.getProperty ("trackColor", 0);
    return SpaceUI::resolveColour (stored, juce::jmax (0, slotIndex));
}

void SpaceTrackProcessor::setRoom (float size, float amount)
{
    size = juce::jlimit (0.0f, 1.0f, size);
    amount = juce::jlimit (0.0f, 1.0f, amount);
    roomSize.store (size);
    roomAmount.store (amount);

    if (std::abs ((float) apvts.state.getProperty ("roomSize", 0.4f) - size) > 0.0005f)
        apvts.state.setProperty ("roomSize", size, nullptr);
    if (std::abs ((float) apvts.state.getProperty ("roomAmount", 0.25f) - amount) > 0.0005f)
        apvts.state.setProperty ("roomAmount", amount, nullptr);
}

void SpaceTrackProcessor::beginAllGestures()
{
    for (auto* id : { "x", "y", "z" })
        apvts.getParameter (id)->beginChangeGesture();
}

void SpaceTrackProcessor::endAllGestures()
{
    for (auto* id : { "x", "y", "z" })
        apvts.getParameter (id)->endChangeGesture();
}

void SpaceTrackProcessor::claimSlot()
{
    auto* reg = getSharedRegistry();
    if (reg == nullptr) return;

    const auto now = juce::Time::currentTimeMillis();
    for (int i = 0; i < SharedRegistry::maxSlots; ++i)
    {
        auto& s = reg->slots[i];
        uint64_t cur = s.owner.load();
        const bool available = (cur == 0) || (now - s.heartbeat.load() > 15000);
        if (! available) continue;

        if (s.owner.compare_exchange_strong (cur, myId))
        {
            slotIndex = i;
            s.heartbeat.store (now);
            s.dragging.store (0);
            // ignore any commands left behind by a previous owner
            lastCmdSeq   = s.cmdSeq.load();
            lastNameSeq  = s.nameSeq.load();
            lastModeSeq  = s.modeSeq.load();
            lastColorSeq = s.colorSeq.load();
            return;
        }
    }
}

void SpaceTrackProcessor::timerCallback()
{
    auto* reg = getSharedRegistry();
    if (reg == nullptr) return;

    if (slotIndex < 0 || reg->slots[slotIndex].owner.load() != myId)
    {
        if (gestureOpen) { endAllGestures(); gestureOpen = false; }
        slotIndex = -1;
        claimSlot();
        if (slotIndex < 0) return;
    }

    auto& s = reg->slots[slotIndex];
    const auto now = juce::Time::currentTimeMillis();
    s.heartbeat.store (now);

    // Follow SpaceMaster's room while one is running; otherwise keep the last settings
    if (now - reg->roomBeat.load() < 2000)
        setRoom (reg->roomSize.load(), reg->roomAmount.load());

    auto setIfChanged = [this] (const char* id, float v)
    {
        auto* p = apvts.getParameter (id);
        const float target = p->convertTo0to1 (juce::jlimit (-1.0f, 1.0f, v));
        if (std::abs (p->getValue() - target) > 1.0e-5f)
            p->setValueNotifyingHost (target);
    };

    // While the master is dragging, keep ONE gesture open for the whole drag.
    // The host then records a single undo step instead of one per update.
    // (A stale flag from a vanished master is ignored after 1.5 s.)
    const bool masterDragging = s.dragging.load() != 0 && (now - s.dragBeat.load()) < 1500;

    if (masterDragging && ! gestureOpen)
    {
        beginAllGestures();
        gestureOpen = true;
    }

    const auto seq = s.cmdSeq.load();
    if (seq != lastCmdSeq)
    {
        lastCmdSeq = seq;
        const bool temporary = ! gestureOpen;      // one-off move (e.g. Center) = one undo step
        if (temporary) beginAllGestures();
        setIfChanged ("x", s.cmdX.load());
        setIfChanged ("y", s.cmdY.load());
        setIfChanged ("z", s.cmdZ.load());
        if (temporary) endAllGestures();
    }

    if (gestureOpen && ! masterDragging)
    {
        endAllGestures();
        gestureOpen = false;
    }

    // 2D/3D switch requested by the master
    const auto mseq = s.modeSeq.load();
    if (mseq != lastModeSeq)
    {
        lastModeSeq = mseq;
        auto* p = apvts.getParameter ("mode3d");
        const float target = s.cmdMode.load() != 0 ? 1.0f : 0.0f;
        if (std::abs (p->getValue() - target) > 0.5f)
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (target);
            p->endChangeGesture();
        }
    }

    // Colour / name requested by the master (saved with the project, not host parameters)
    const auto cseq = s.colorSeq.load();
    if (cseq != lastColorSeq)
    {
        lastColorSeq = cseq;
        apvts.state.setProperty ("trackColor", (int) s.cmdColor.load(), nullptr);
    }

    const auto nseq = s.nameSeq.load();
    if (nseq != lastNameSeq)
    {
        lastNameSeq = nseq;
        char buf[32];
        std::memcpy (buf, s.cmdName, sizeof (buf));
        buf[31] = 0;
        apvts.state.setProperty ("trackName", juce::String::fromUTF8 (buf), nullptr);
    }

    // Publish our state for the master
    s.x.store (*apvts.getRawParameterValue ("x"));
    s.y.store (*apvts.getRawParameterValue ("y"));
    s.z.store (*apvts.getRawParameterValue ("z"));
    s.mode3d.store (*apvts.getRawParameterValue ("mode3d") > 0.5f ? 1u : 0u);
    s.color.store ((juce::uint32) (int) apvts.state.getProperty ("trackColor", 0));
    apvts.state.getProperty ("trackName").toString().copyToUTF8 (s.name, sizeof (s.name));
}

bool SpaceTrackProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
        && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void SpaceTrackProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;

    juce::dsp::ProcessSpec spec2 { sampleRate, (juce::uint32) samplesPerBlock, 2 };
    lowpass.prepare (spec2);
    lowpass.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    lowpass.reset();

    juce::dsp::ProcessSpec spec1 { sampleRate, (juce::uint32) samplesPerBlock, 1 };
    elevDownLP.prepare (spec1);
    elevDownLP.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    elevDownLP.setCutoffFrequency (2500.0f);
    elevDownLP.reset();

    hrtfVoice.prepare (Hrtf::Table::get (HrtfData::hrtf_kemar_bin, (size_t) HrtfData::hrtf_kemar_binSize, sampleRate));
    earlyRoom.prepare (sampleRate);
    lateRoom.setSampleRate (sampleRate);
    lateRoom.reset();
    lastRoomSize = -1.0f;
    roomGain = roomAmount.load();
    controlCounter = 0;
    path3DActive = *apvts.getRawParameterValue ("mode3d") > 0.5f;

    float ux, uy, uz, dist;
    directionOf (*apvts.getRawParameterValue ("x"), *apvts.getRawParameterValue ("y"), *apvts.getRawParameterValue ("z"), ux, uy, uz, dist);
    hrtfVoice.setDirection (ux, uy, uz);
    earlyRoom.setGeometry (*apvts.getRawParameterValue ("x"), *apvts.getRawParameterValue ("y"), *apvts.getRawParameterValue ("z"), roomSize.load());

    xSmooth.reset (sampleRate, 0.03);
    ySmooth.reset (sampleRate, 0.03);
    zSmooth.reset (sampleRate, 0.03);
    modeSmooth.reset (sampleRate, 0.05);
    xSmooth.setCurrentAndTargetValue (*apvts.getRawParameterValue ("x"));
    ySmooth.setCurrentAndTargetValue (*apvts.getRawParameterValue ("y"));
    zSmooth.setCurrentAndTargetValue (*apvts.getRawParameterValue ("z"));
    modeSmooth.setCurrentAndTargetValue (*apvts.getRawParameterValue ("mode3d") > 0.5f ? 1.0f : 0.0f);
}

void SpaceTrackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumChannels() < 2) return;

    xSmooth.setTargetValue (*apvts.getRawParameterValue ("x"));
    ySmooth.setTargetValue (*apvts.getRawParameterValue ("y"));
    zSmooth.setTargetValue (*apvts.getRawParameterValue ("z"));
    modeSmooth.setTargetValue (*apvts.getRawParameterValue ("mode3d") > 0.5f ? 1.0f : 0.0f);

    const float ty = ySmooth.getTargetValue();
    const float maxCut = (float) sr * 0.45f;

    // ---- block-level filter settings ----
    // 2D: the further back, the darker (20 kHz -> 3 kHz)
    lowpass.setCutoffFrequency (juce::jmin (20000.0f * std::pow (0.15f, juce::jmax (0.0f, -ty)), maxCut));

    // The 3D path only runs while it can be heard, so a track in stereo costs almost nothing
    const bool want3D = modeSmooth.getTargetValue() > 0.5f || modeSmooth.getCurrentValue() > 0.0001f;
    if (want3D && ! path3DActive)
    {
        hrtfVoice.reset();
        earlyRoom.reset();
        lateRoom.reset();
    }
    path3DActive = want3D;

    const float roomTarget = roomAmount.load();
    const float roomSizeNow = roomSize.load();
    if (want3D && roomSizeNow != lastRoomSize)
    {
        lastRoomSize = roomSizeNow;
        juce::Reverb::Parameters rp;
        rp.roomSize = 0.35f + 0.55f * roomSizeNow;
        rp.damping = 0.5f;
        rp.wetLevel = 0.5f;
        rp.dryLevel = 0.0f;
        rp.width = 1.0f;
        lateRoom.setParameters (rp);
    }

    auto* l = buffer.getWritePointer (0);
    auto* r = buffer.getWritePointer (1);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float x = xSmooth.getNextValue();
        const float y = ySmooth.getNextValue();
        const float z = zSmooth.getNextValue();
        const float mode = modeSmooth.getNextValue();
        const float inL = l[i], inR = r[i];

        // ================= 2D path (same as before) =================
        const float back  = juce::jmax (0.0f, -y);
        const float front = juce::jmax (0.0f,  y);
        const float gain2D = juce::Decibels::decibelsToGain (-9.0f * back + 2.0f * front);

        const float angle = (x + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const float gL = juce::jmin (1.0f, std::cos (angle) * juce::MathConstants<float>::sqrt2);
        const float gR = juce::jmin (1.0f, std::sin (angle) * juce::MathConstants<float>::sqrt2);

        float L2 = lowpass.processSample (0, inL);
        float R2 = lowpass.processSample (1, inR);
        const float mid  = 0.5f * (L2 + R2);
        const float side = 0.5f * (L2 - R2) * (1.0f - 0.5f * back);
        L2 = (mid + side) * gL * gain2D;
        R2 = (mid - side) * gR * gain2D;

        // ================= 3D path: measured HRTF plus the shared room =================
        float L3 = 0.0f, R3 = 0.0f;
        if (want3D)
        {
            float ux, uy, uz, dist;
            directionOf (x, y, z, ux, uy, uz, dist);
            const float w = juce::jmin (1.0f, dist * 2.5f);      // 0 = inside the head, 1 = fully positioned

            if (++controlCounter >= 32)
            {
                controlCounter = 0;
                hrtfVoice.setDirection (ux, uy, uz);
                earlyRoom.setGeometry (x, y, z, roomSizeNow);
            }

            const float mono = 0.5f * (inL + inR);               // the sound becomes a point source

            // The measurements stop 40 degrees below ear level, so further down fades to a muffled copy
            const float below = juce::jlimit (0.0f, 1.0f, (-uz - 0.64f) / 0.36f) * w;
            const float lpS = elevDownLP.processSample (0, mono);
            const float src = mono + (lpS - mono) * below;

            float bL, bR, dry;
            hrtfVoice.process (src, bL, bR, dry);

            const float distGain = 1.0f / (1.0f + 0.8f * dist);
            L3 = ((1.0f - w) * dry + w * bL) * distGain;
            R3 = ((1.0f - w) * dry + w * bR) * distGain;

            // Further away means less direct sound and relatively more room
            roomGain += 0.0005f * (roomTarget - roomGain);
            if (roomGain > 0.0005f)
            {
                float earlyL, earlyR;
                earlyRoom.process (src, earlyL, earlyR);
                float lateL = src * (0.6f + 0.6f * dist);
                float lateR = lateL;
                lateRoom.processStereo (&lateL, &lateR, 1);
                L3 += roomGain * (0.9f * earlyL + 0.5f * lateL);
                R3 += roomGain * (0.9f * earlyR + 0.5f * lateR);
            }
        }

        // crossfade between the two paths when the mode changes
        l[i] = L2 * (1.0f - mode) + L3 * mode;
        r[i] = R2 * (1.0f - mode) + R3 * mode;
    }
}

juce::AudioProcessorEditor* SpaceTrackProcessor::createEditor()
{
    return new SpaceTrackEditor (*this);
}

void SpaceTrackProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void SpaceTrackProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            roomSize.store (juce::jlimit (0.0f, 1.0f, (float) apvts.state.getProperty ("roomSize", 0.4f)));
            roomAmount.store (juce::jlimit (0.0f, 1.0f, (float) apvts.state.getProperty ("roomAmount", 0.25f)));
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpaceTrackProcessor();
}
