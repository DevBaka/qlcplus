/*
  Q Light Controller Plus
  vcmusicreactive.cpp

  Copyright (c) Massimo Callegari

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QDebug>

#include "vcmusicreactive.h"
#include "virtualconsole.h"
#include "qlcinputsource.h"
#include "audiocapture.h"
#include "audioevents.h"
#include "chaseraction.h"
#include "mastertimer.h"
#include "chaser.h"
#include "show.h"
#include "doc.h"

#define KXMLQLCVCMusicReactiveIntensities QStringLiteral("Intensities")
#define KXMLQLCVCMusicReactiveThresholds  QStringLiteral("Thresholds")
#define KXMLQLCVCMusicReactiveActive      QStringLiteral("Active")
#define KXMLQLCVCMusicReactiveAmbient     QStringLiteral("Ambient")
#define KXMLQLCVCMusicReactiveEnabled     QStringLiteral("Enabled")
#define KXMLQLCVCMusicReactiveFunctionID  QStringLiteral("FunctionID")
#define KXMLQLCVCMusicReactiveAutoMode    QStringLiteral("AutoMode")
#define KXMLQLCVCMusicReactiveAdvanceOnBeat QStringLiteral("AdvanceOnBeat")
#define KXMLQLCVCMusicReactiveProfile      QStringLiteral("Profile")
#define KXMLQLCVCMusicReactiveProfileName  QStringLiteral("Name")
#define KXMLQLCVCMusicReactiveProfileChaser QStringLiteral("ProfileChaser")
// Pre-Profiles format (a flat Chaser list, one at most "active" -
// see loadXML()'s legacy-migration branch, which folds these into an
// implicit "Profile 1" so an existing project's setup isn't silently
// dropped just because it predates Profiles).
#define KXMLQLCVCMusicReactiveChaser       QStringLiteral("Chaser")
#define KXMLQLCVCMusicReactiveAmbientSet   QStringLiteral("AmbientSet")
#define KXMLQLCVCMusicReactiveAmbientSetFunction QStringLiteral("AmbientSetFunction")
#define KXMLQLCVCMusicReactiveGroup        QStringLiteral("Group")
#define KXMLQLCVCMusicReactiveGroupId      QStringLiteral("Id")
#define KXMLQLCVCMusicReactiveGroupRotate  QStringLiteral("Rotate")
#define KXMLQLCVCMusicReactiveGroupInterval QStringLiteral("IntervalSec")
#define KXMLQLCVCMusicReactiveGroupMember  QStringLiteral("GroupMember")
#define KXMLQLCVCMusicReactiveIsGroup      QStringLiteral("IsGroup")
#define KXMLQLCVCMusicReactiveSuperChaser      QStringLiteral("SuperChaser")
#define KXMLQLCVCMusicReactiveSuperChaserTrack QStringLiteral("SuperChaserTrack")
#define KXMLQLCVCMusicReactiveIsSuperChaser    QStringLiteral("IsSuperChaser")
#define KXMLQLCVCMusicReactiveSuperShow        QStringLiteral("SuperShow")
#define KXMLQLCVCMusicReactiveSuperShowSection QStringLiteral("SuperShowSection")
#define KXMLQLCVCMusicReactiveSuperShowLoop    QStringLiteral("Loop")
#define KXMLQLCVCMusicReactiveProfileID        QStringLiteral("ProfileID")
#define KXMLQLCVCMusicReactiveDurationSec      QStringLiteral("DurationSec")

VCMusicReactive::VCMusicReactive(Doc *doc, VirtualConsole *vc, QObject *parent)
    : VCWidget(doc, parent)
    , m_vc(vc)
    , m_inputCapture(nullptr)
    , m_captureEnabled(false)
    , m_inBreak(false)
{
    setType(VCWidget::MusicReactiveWidget);

    for (int i = 0; i < BandCount; i++)
    {
        m_intensities[i] = 100;
        m_thresholds[i] = 60;
    }

    m_levels << 0 << 0 << 0;

    qDebug() << "[VCMusicReactive] constructed, id" << id();
}

VCMusicReactive::~VCMusicReactive()
{
    qDebug() << "[VCMusicReactive] destroyed, id" << id();

    if (m_captureEnabled)
        setCaptureEnabled(false);
}

QString VCMusicReactive::defaultCaption() const
{
    return tr("Music Reactive %1").arg(id() + 1);
}

void VCMusicReactive::setupLookAndFeel(qreal pixelDensity, int page)
{
    setPage(page);
    QFont wFont = font();
    wFont.setBold(true);
    wFont.setPointSize(pixelDensity * 5.0);
    setFont(wFont);
}

void VCMusicReactive::render(QQuickView *view, QQuickItem *parent)
{
    if (view == nullptr || parent == nullptr)
        return;

    QQmlComponent *component = new QQmlComponent(view->engine(), QUrl("qrc:/VCMusicReactiveItem.qml"));

    if (component->isError())
    {
        qDebug() << component->errors();
        delete component;
        return;
    }

    m_item = qobject_cast<QQuickItem*>(component->create());
    if (m_item == nullptr)
        qWarning() << Q_FUNC_INFO << "Unable to create music reactive component" << component->errors();
    delete component;
    if (m_item == nullptr)
        return;

    m_item->setParentItem(parent);
    m_item->setProperty("musicReactiveObj", QVariant::fromValue(this));
}

QString VCMusicReactive::propertiesResource() const
{
    return QString("qrc:/VCMusicReactiveProperties.qml");
}

VCWidget *VCMusicReactive::createCopy(VCWidget *parent) const
{
    Q_ASSERT(parent != nullptr);

    VCMusicReactive *copy = new VCMusicReactive(m_doc, m_vc, parent);
    if (copy->copyFrom(this) == false)
    {
        delete copy;
        copy = nullptr;
    }

    return copy;
}

bool VCMusicReactive::copyFrom(const VCWidget *widget)
{
    const VCMusicReactive *mr = qobject_cast<const VCMusicReactive*>(widget);
    if (mr == nullptr)
        return false;

    for (int i = 0; i < BandCount; i++)
    {
        m_intensities[i] = mr->m_intensities[i];
        m_thresholds[i] = mr->m_thresholds[i];
    }
    m_profiles = mr->m_profiles;
    m_currentProfileIndex = mr->m_currentProfileIndex;
    m_nextProfileId = mr->m_nextProfileId;
    m_ambientSets = mr->m_ambientSets;
    m_currentAmbientSetIndex = mr->m_currentAmbientSetIndex;
    m_nextAmbientSetId = mr->m_nextAmbientSetId;
    m_groups = mr->m_groups;
    m_nextGroupId = mr->m_nextGroupId;
    m_superChasers = mr->m_superChasers;
    m_nextSuperChaserId = mr->m_nextSuperChaserId;
    m_superShows = mr->m_superShows;
    m_nextSuperShowId = mr->m_nextSuperShowId;
    // Playback state (m_activeSuperShowIndex/m_superShowTimer) deliberately
    // NOT copied - a fresh copy always starts with every Super Show
    // stopped, regardless of whether the source was playing one.
    m_autoMode = mr->m_autoMode;
    m_advanceOnBeat = mr->m_advanceOnBeat;

    // Not a copy of mr->m_midiControlKeyToId - this copy's own
    // m_externalControlList (base class, per-instance) starts out empty
    // regardless, so the mapping has to be freshly (re)established
    // against it rather than blindly reusing the source widget's ids.
    syncMidiControls();

    return VCWidget::copyFrom(widget);
}

void VCMusicReactive::remapChannels(const QMap<SceneValue, SceneValue> &remapMap)
{
    Q_UNUSED(remapMap)
    // No direct DMX channels in this widget - it only ever starts/steps
    // whole Functions, which remap themselves.
}

FunctionParent VCMusicReactive::functionParent() const
{
    return FunctionParent(FunctionParent::AutoVCWidget, id());
}

/*********************************************************************
 * Capture
 *********************************************************************/

bool VCMusicReactive::captureEnabled() const
{
    return m_captureEnabled;
}

void VCMusicReactive::setCaptureEnabled(bool enable)
{
    if (enable == m_captureEnabled)
        return;

    m_captureEnabled = enable;

    QSharedPointer<AudioCapture> capture(m_doc->audioInputCapture());
    m_inputCapture = capture.data();

    if (enable)
    {
        connect(m_inputCapture, SIGNAL(analysisStateChanged(double,double,int,int,double,double,double,double,double,double,bool,double,double,double)),
                this, SLOT(slotAnalysisStateChanged(double,double,int,int,double,double,double,double,double,double,bool,double,double,double)));
        connect(m_inputCapture, SIGNAL(audioEventDetected(int,double,double,double)),
                this, SLOT(slotAudioEventDetected(int,double,double,double)));
        // No FFT bands are actually needed by this widget (it consumes
        // analysisStateChanged/audioEventDetected, not dataProcessed),
        // but AudioCapture only starts its thread once at least one band
        // count is registered - register the smallest possible amount.
        m_inputCapture->registerBandsNumber(1);

        // Pick up whichever Profile(s) were left active (e.g. freshly
        // loaded from a project) - without this, capture could be
        // running with an active Profile that never actually starts
        // until a rotation happens to land on it.
        if (m_currentProfileIndex < 0)
            rotateToNextActiveProfile();
    }
    else
    {
        m_inputCapture->unregisterBandsNumber(1);
        disconnect(m_inputCapture, SIGNAL(analysisStateChanged(double,double,int,int,double,double,double,double,double,double,bool,double,double,double)),
                   this, SLOT(slotAnalysisStateChanged(double,double,int,int,double,double,double,double,double,double,bool,double,double,double)));
        disconnect(m_inputCapture, SIGNAL(audioEventDetected(int,double,double,double)),
                   this, SLOT(slotAudioEventDetected(int,double,double,double)));

        stopSuperShow();
        stopCurrentProfileChasers();
        stopCurrentAmbientSetFunctions();
        m_inBreak = false;
        emit inBreakChanged();
    }

    emit captureEnabledChanged();
}

/*********************************************************************
 * Levels / intensity bands
 *********************************************************************/

QVariantList VCMusicReactive::levels() const
{
    return m_levels;
}

QVariantList VCMusicReactive::intensities() const
{
    QVariantList list;
    for (int i = 0; i < BandCount; i++)
        list << m_intensities[i];
    return list;
}

void VCMusicReactive::setIntensity(int band, int percent)
{
    if (band < 0 || band >= BandCount)
        return;

    m_intensities[band] = qBound(0, percent, 100);
    emit intensitiesChanged();
}

QVariantList VCMusicReactive::thresholds() const
{
    QVariantList list;
    for (int i = 0; i < BandCount; i++)
        list << m_thresholds[i];
    return list;
}

void VCMusicReactive::setThreshold(int band, int percent)
{
    if (band < 0 || band >= BandCount)
        return;

    m_thresholds[band] = qBound(0, percent, 100);
    emit thresholdsChanged();
}

bool VCMusicReactive::inBreak() const
{
    return m_inBreak;
}

bool VCMusicReactive::autoMode() const
{
    return m_autoMode;
}

void VCMusicReactive::setAutoMode(bool enable)
{
    if (enable == m_autoMode)
        return;

    m_autoMode = enable;
    emit autoModeChanged();
    // Switching back to Auto does NOT immediately re-sync m_inBreak to
    // whatever the automatic decision currently reads - it just resumes
    // being driven by it from here on, same as it always was before any
    // manual override. The next analysisStateChanged block will apply
    // a transition itself if the automatic and manual states actually
    // disagree.
}

void VCMusicReactive::setManualBreak(bool enable)
{
    if (m_autoMode)
        return; // only meaningful in manual mode - see autoMode()

    applyBreakTransition(enable);
}

bool VCMusicReactive::advanceOnBeat() const
{
    return m_advanceOnBeat;
}

void VCMusicReactive::setAdvanceOnBeat(bool enable)
{
    if (enable == m_advanceOnBeat)
        return;

    m_advanceOnBeat = enable;
    emit advanceOnBeatChanged();
}

void VCMusicReactive::applyBreakTransition(bool newBreak)
{
    if (newBreak == m_inBreak)
        return;

    // Logged unconditionally (not just via debugText) so a transition
    // that happens to land between two debugText reads - or a rapid
    // flip-flop, which is exactly the failure mode the full-window fix
    // above targets - still shows up plainly in the log with real
    // timing, instead of only ever being visible as a single static
    // snapshot.
    qDebug() << "[VCMusicReactive] applyBreakTransition:" << (m_inBreak ? "break" : "active")
             << "->" << (newBreak ? "break" : "active")
             << "sinceLastTransitionMs" << (m_lastAutoTransitionTimerValid ? m_lastAutoTransitionTimer.elapsed() : -1);

    m_inBreak = newBreak;
    emit inBreakChanged();

    if (newBreak)
    {
        stopCurrentProfileChasers();
        // Rotate BEFORE starting - every time a break is entered, not
        // partway through one still going (an already-running idle
        // combo shouldn't jump mid-idle), so a long set of quiet
        // sections through a track each get a different idle combo
        // rather than the exact same one every single time.
        rotateToNextActiveAmbientSet();
        startCurrentAmbientSetFunctions();
    }
    else
    {
        stopCurrentAmbientSetFunctions();
        // The current Profile's chasers resume on the next detected
        // Kick/Beat (advanceCurrentProfileChasers() starts any that
        // aren't running).
    }
}

QString VCMusicReactive::debugText() const
{
    return m_debugText;
}

void VCMusicReactive::slotAnalysisStateChanged(double bpm, double beatPhase, int beatIndex, int barIndex,
                                               double energy, double bassEnergy, double midEnergy, double highEnergy,
                                               double kickConfidence, double beatConfidence, bool inBreak,
                                               double peakBandValue, double peakThreshold, double kickBandOnset)
{
    Q_UNUSED(beatIndex)
    Q_UNUSED(highEnergy)
    Q_UNUSED(kickConfidence)
    Q_UNUSED(beatConfidence)
    Q_UNUSED(inBreak) // superseded by the on-grid-Kick decision below
    Q_UNUSED(kickBandOnset)

    // Cache the continuous beat-grid reading so the next Kick/Beat event
    // (arrives as a separate, asynchronous signal) can check "was this
    // near a predicted beat" / "was there energy at this beat" - see
    // slotAudioEventDetected().
    m_lastBeatPhase = beatPhase;
    m_lastBpm = bpm;
    m_lastBassEnergy = bassEnergy;

    // Wall-clock Group rotation (10s/30s/... - not Beat/Kick-synced,
    // hence being driven from here rather than slotAudioEventDetected())
    // - see the class comment above groups() in vcmusicreactive.h.
    tickGroupRotations();

    // Wall-clock Super Show playback (section 1 for N seconds, section 2
    // for M seconds, ...) - same reasoning as Group rotation just above:
    // not Beat/Kick-synced, so driven from here. See the class comment
    // above superShows() in vcmusicreactive.h.
    tickSuperShow();

    // Profile rotation: with more than one Profile active, automatically
    // switch which one is in control every kRotateEveryBars bars - see
    // the class comment in vcmusicreactive.h for why (this is what
    // gives "different combinations of chasers coming back around"
    // rather than either "only one Profile ever runs" or "every active
    // Profile's chasers pile up and run forever simultaneously"). Only
    // one active Profile just stays current - no rotation, no churn.
    // Suspended during a break: the current Profile's chasers are
    // stopped then anyway (Ambient has taken over), so rotating would
    // just silently swap which one resumes once the break ends.
    if (barIndex >= 0 && barIndex != m_lastRotateBarIndex)
    {
        if (m_lastRotateBarIndex < 0)
        {
            m_lastRotateBarIndex = barIndex; // start tracking from here
        }
        else
        {
            static constexpr int kRotateEveryBars = 8;
            if (!m_inBreak && barIndex - m_lastRotateBarIndex >= kRotateEveryBars)
            {
                int activeCount = 0;
                for (const MusicProfile &p : m_profiles)
                    if (p.active) activeCount++;
                if (activeCount > 1)
                    rotateToNextActiveProfile();
                m_lastRotateBarIndex = barIndex;
            }
        }
    }

    // Energy values are unbounded onset-band sums, not 0..1 - scale each
    // one against a fixed reference so the meters read sensibly without
    // per-track calibration, then apply the user's own per-band gain.
    static constexpr double kEnergyRef = 0.3;
    auto toLevel = [](double e, int gainPercent) -> int
    {
        double v = qBound(0.0, e / kEnergyRef, 1.0);
        v *= (double(gainPercent) / 100.0);
        return qBound(0, int(v * 255.0 + 0.5), 255);
    };

    m_levels.clear();
    m_levels << toLevel(energy, m_intensities[Master])
             << toLevel(bassEnergy, m_intensities[Kick])
             << toLevel(midEnergy, m_intensities[Mids]);
    emit levelsChanged();

    // Break = fewer than half of the last few tracked Beats actually
    // had kick-band energy on them - see slotAudioEventDetected()'s
    // Beat handling for where m_recentBeatHits gets filled in (that's
    // where kBeatHitFloor lives). Literally "is there still a kick
    // roughly every quarter note", checked at the tempo tracker's own
    // reliable beat timing - not a volume/energy threshold on its own
    // (a breakdown can still be loud - pads, vocals, a sub riser - and
    // a good mix rarely goes near-silent) and not gated on the sparse
    // Kick classifier's own timing either (confirmed against 4 real
    // recordings via engine/test/wavanalyze: that flickered Active/
    // Ambient every ~1.8s mid-song, regardless of hold value - individual
    // Kicks are classified far too sparsely to time a decision on).
    // Require the window to be FULLY filled before ever declaring break.
    // This used to trigger off just half the window (2 of 4 slots), which
    // meant a Profile that had just gone active - or capture that had
    // just been enabled - could read "break" off its first two beats
    // alone. Two misses is well within normal luck (tempo lock still
    // settling, level/AGC not stabilized yet, a quiet pickup) and was
    // observed to spuriously kill a freshly-started Profile's Chasers
    // within a beat or two of starting, then restart them from step 0
    // on the next beat, over and over - which looks exactly like "the
    // chaser is stuck on its first step forever", even though nothing
    // is actually wrong with ChaserRunner's step advance itself. Full
    // window = a real, sustained absence of kicks, not a coin flip.
    static constexpr int kBeatWindowSize = 4;
    int hits = 0;
    for (bool h : m_recentBeatHits)
        if (h) hits++;
    bool computedInBreak = m_recentBeatHits.size() >= kBeatWindowSize
                         && hits * 2 < m_recentBeatHits.size();

    int thresholdLevel = qRound(m_thresholds[Kick] * 255.0 / 100.0);
    m_debugText = QString("kick raw=%1 lvl=%2/%3 energy=%4 inBreak=%5 (beatHits %6/%7) kickEvents=%8 onGridMs=%9 beatEvents=%10 clapEvents=%11 bpm=%12 beatPhase=%13 | onset peak=%14 gate=%15 | lastKickStrength=%16/%3 | adv: calls=%17 break=%18 rateLtd=%19 ok=%20 | step: calls=%21 fresh=%22 next=%23 | chasers: %24")
                      .arg(bassEnergy, 0, 'f', 4)
                      .arg(m_levels[Kick].toInt())
                      .arg(thresholdLevel)
                      .arg(energy, 0, 'f', 4)
                      .arg(computedInBreak ? "yes" : "no")
                      .arg(hits)
                      .arg(m_recentBeatHits.size())
                      .arg(m_kickEventCount)
                      .arg(m_lastOnGridKickTimerValid ? m_lastOnGridKickTimer.elapsed() : -1)
                      .arg(m_beatEventCount)
                      .arg(m_clapEventCount)
                      .arg(bpm, 0, 'f', 1)
                      .arg(beatPhase, 0, 'f', 2)
                      .arg(peakBandValue, 0, 'f', 4)
                      .arg(peakThreshold, 0, 'f', 4)
                      .arg(m_lastKickStrengthLevel)
                      .arg(m_tryAdvanceCalls)
                      .arg(m_tryAdvanceBlockedBreak)
                      .arg(m_tryAdvanceRateLimited)
                      .arg(m_tryAdvanceAccepted)
                      .arg(m_startOrStepCalls)
                      .arg(m_startOrStepFreshStarts)
                      .arg(m_startOrStepAdvances)
                      .arg(m_lastChaserStatus.isEmpty() ? "(none yet)" : m_lastChaserStatus);
    emit debugTextChanged();

    // In manual mode the operator's own setManualBreak() call owns
    // m_inBreak - the automatic decision above still gets computed
    // (visible in debugText, so re-enabling Auto picks up a sane
    // current reading immediately) but must not act on it.
    if (m_autoMode && computedInBreak != m_inBreak)
    {
        // Rate-limit ENTERING break only (starts ambient Functions -
        // that's the direction that can stack/escalate if it repeats
        // faster than a fade completes). LEAVING break must always be
        // immediate - a real kick resuming is exactly the case that
        // has to feel instant, and there's nothing to start rapidly
        // there (stopCurrentProfileChasers()/stopCurrentAmbientSetFunctions()
        // are the safe direction). An earlier version rate-limited both
        // directions and that made real kicks feel like they weren't
        // being recognized - up to 2s of silence after the music
        // resumed before anything actually happened again.
        static constexpr qint64 kMinEnterBreakGapMs = 2000;
        bool rateLimited = computedInBreak
                         && m_lastAutoTransitionTimerValid
                         && m_lastAutoTransitionTimer.elapsed() < kMinEnterBreakGapMs;
        if (!rateLimited)
        {
            applyBreakTransition(computedInBreak);
            if (computedInBreak)
            {
                m_lastAutoTransitionTimer.start();
                m_lastAutoTransitionTimerValid = true;
            }
        }
    }
}

void VCMusicReactive::slotAudioEventDetected(int type, double timestampSec, double confidence, double strength)
{
    Q_UNUSED(timestampSec)

    if (type == int(AudioEventType::Snare))
    {
        // Snare/clap - diagnostic only for now (debugText's clapEvents),
        // not wired to any action yet.
        m_clapEventCount++;
        emit debugTextChanged();
        return;
    }

    if (type == int(AudioEventType::Beat))
    {
        // Sample bassEnergy (cached from the last analysisStateChanged
        // block) right at this reliably-timed Beat - "was there
        // actually a kick roughly on this quarter note" - and push into
        // the rolling window slotAnalysisStateChanged() reads for the
        // Ambient/Active decision. See kBeatHitFloor there for why this
        // (not the sparse Kick classifier) drives the window, and the
        // class comment for the volume-threshold approaches rejected
        // before that. Always tracked regardless of advanceOnBeat - the
        // window is what decides Ambient/Active, independent of what
        // advances the chaser.
        static constexpr double kBeatHitFloor = 0.10;
        m_recentBeatHits.append(m_lastBassEnergy >= kBeatHitFloor);
        static constexpr int kBeatWindowSize = 4;
        while (m_recentBeatHits.size() > kBeatWindowSize)
            m_recentBeatHits.removeFirst();

        m_beatEventCount++;
        emit debugTextChanged();

        if (m_advanceOnBeat)
            tryAdvanceChaser(confidence, "beat");
        return;
    }

    if (type == int(AudioEventType::Kick))
    {
        // Unconditional - counts every Kick the engine ever reports,
        // regardless of what happens to it below.
        m_kickEventCount++;

        static constexpr double kStrengthCeiling = 2.0 / 3.0; // see percussioneventdetector.cpp
        m_lastKickStrengthLevel = qBound(0, int((strength / kStrengthCeiling) * 255.0 + 0.5), 255);

        // "Is this Kick roughly where a beat was expected" - diagnostic
        // only (debugText's onGridMs), using beatPhase/bpm cached from
        // the last analysisStateChanged block (0 = exactly on the beat,
        // 0.5 = exactly halfway between two beats).
        static constexpr double kOnGridToleranceFrac = 0.2; // +/-20% of a beat
        bool onGrid = m_lastBpm > 0.0
                   && (m_lastBeatPhase <= kOnGridToleranceFrac
                       || m_lastBeatPhase >= 1.0 - kOnGridToleranceFrac);
        if (onGrid)
        {
            m_lastOnGridKickTimer.start();
            m_lastOnGridKickTimerValid = true;
        }

        emit debugTextChanged();

        if (!m_advanceOnBeat)
            tryAdvanceChaser(confidence, "kick");
    }
}

void VCMusicReactive::tryAdvanceChaser(double confidence, const char *reason)
{
    m_tryAdvanceCalls++;

    if (m_inBreak)
    {
        m_tryAdvanceBlockedBreak++;
        qDebug() << "[VCMusicReactive]" << reason << "ignored: currently in break/ambient mode";
        return;
    }

    // Safety cap, independent of anything the onset/beat detector
    // itself guarantees: never advance faster than 300 BPM (200ms)
    // allows - generously above any real track's rate, just a hard
    // backstop against a runaway advance rate stacking overlapping
    // fades on real light hardware regardless of its actual cause.
    static constexpr qint64 kMinAdvanceGapMs = 200;
    if (m_lastAdvanceTimerValid && m_lastAdvanceTimer.elapsed() < kMinAdvanceGapMs)
    {
        m_tryAdvanceRateLimited++;
        qDebug() << "[VCMusicReactive]" << reason << "ignored: rate-limited";
        return;
    }
    m_lastAdvanceTimer.start();
    m_lastAdvanceTimerValid = true;
    m_tryAdvanceAccepted++;

    qDebug() << "[VCMusicReactive]" << reason << "accepted, confidence" << confidence
             << "currentProfileIndex" << m_currentProfileIndex << "profileCount" << m_profiles.count();
    advanceCurrentProfileChasers(confidence);
}

/*********************************************************************
 * Profiles - see the class comment in vcmusicreactive.h
 *********************************************************************/

QVariantList VCMusicReactive::profiles() const
{
    QVariantList list;
    for (const MusicProfile &p : m_profiles)
    {
        QVariantMap map;
        map.insert("id", p.id); // stable - needed by e.g. Super Show sections to reference this Profile
        map.insert("name", p.name);
        map.insert("active", p.active);
        map.insert("midiControlId", midiControlIdFor(QString("profile:%1").arg(p.id)));

        // Full chaser list (including disabled ones and ones in
        // inactive Profiles) so the properties panel can build up a
        // Profile's contents before ever activating it - visibleChasers
        // only covers already-active Profiles, which is what the live
        // widget's operational panel wants instead.
        QVariantList chasers;
        for (const FunctionRef &ref : p.chasers)
        {
            QVariantMap cmap;
            cmap.insert("id", ref.id);
            cmap.insert("name", functionOrGroupName(ref));
            cmap.insert("enabled", ref.enabled);
            cmap.insert("isGroup", ref.isGroup);
            cmap.insert("isShow", isShowFunction(ref));
            cmap.insert("isSuperChaser", ref.isSuperChaser);
            cmap.insert("midiControlId", midiControlIdFor(
                QString("chaser:%1:%2:%3").arg(p.id).arg(ref.id).arg(refKindCode(ref))));
            chasers << cmap;
        }
        map.insert("chasers", chasers);

        list << map;
    }
    return list;
}

QVariantList VCMusicReactive::visibleChasers() const
{
    QVariantList list;
    for (int pi = 0; pi < m_profiles.count(); pi++)
    {
        const MusicProfile &p = m_profiles[pi];
        if (!p.active)
            continue;
        for (int ci = 0; ci < p.chasers.count(); ci++)
        {
            const FunctionRef &ref = p.chasers[ci];
            QVariantMap map;
            map.insert("id", ref.id);
            map.insert("name", functionOrGroupName(ref));
            map.insert("enabled", ref.enabled);
            map.insert("running", ref.enabled && pi == m_currentProfileIndex);
            map.insert("profileIndex", pi);
            map.insert("profileName", p.name);
            map.insert("chaserIndex", ci);
            map.insert("isGroup", ref.isGroup);
            map.insert("isShow", isShowFunction(ref));
            map.insert("isSuperChaser", ref.isSuperChaser);
            map.insert("midiControlId", midiControlIdFor(
                QString("chaser:%1:%2:%3").arg(p.id).arg(ref.id).arg(refKindCode(ref))));
            list << map;
        }
    }
    return list;
}

int VCMusicReactive::currentProfileIndex() const
{
    return m_currentProfileIndex;
}

int VCMusicReactive::profileIndexById(quint32 profileId) const
{
    for (int i = 0; i < m_profiles.count(); i++)
        if (m_profiles[i].id == profileId)
            return i;
    return -1;
}

void VCMusicReactive::addProfile(const QString &name)
{
    MusicProfile p;
    p.id = m_nextProfileId++;
    p.name = name.isEmpty() ? tr("Profile %1").arg(m_profiles.count() + 1) : name;
    m_profiles.append(p);
    emit profilesChanged();
    syncMidiControls();
}

void VCMusicReactive::removeProfileAt(int index)
{
    if (index < 0 || index >= m_profiles.count())
        return;

    if (index == m_currentProfileIndex)
    {
        stopCurrentProfileChasers();
        m_currentProfileIndex = -1;
    }
    else if (m_currentProfileIndex > index)
    {
        m_currentProfileIndex--;
    }

    m_profiles.remove(index);

    if (m_currentProfileIndex < 0)
        rotateToNextActiveProfile(); // pick up another active Profile, if any

    emit profilesChanged();
    emit visibleChasersChanged();
    syncMidiControls();
}

void VCMusicReactive::renameProfile(int index, const QString &name)
{
    if (index < 0 || index >= m_profiles.count() || name.isEmpty())
        return;

    m_profiles[index].name = name;
    emit profilesChanged();
    emit visibleChasersChanged();
    syncMidiControls(); // the Profile's name is embedded in its control label
}

void VCMusicReactive::setProfileActive(int index, bool active)
{
    if (index < 0 || index >= m_profiles.count() || m_profiles[index].active == active)
        return;

    m_profiles[index].active = active;
    emit profilesChanged();

    QString key = QString("profile:%1").arg(m_profiles[index].id);
    if (m_midiControlKeyToId.contains(key))
        sendControlFeedback(m_midiControlKeyToId.value(key), active);

    if (active)
    {
        // First Profile to go active: become current immediately. If
        // one is already current, just join the rotation pool - the
        // next bar-boundary rotation will get to it in turn.
        if (m_currentProfileIndex < 0)
        {
            m_currentProfileIndex = index;
            startCurrentProfileChasers();
        }
    }
    else if (index == m_currentProfileIndex)
    {
        stopCurrentProfileChasers();
        m_currentProfileIndex = -1;
        rotateToNextActiveProfile();
    }

    emit visibleChasersChanged();
}

void VCMusicReactive::addChaserToProfile(int profileIndex, quint32 functionId)
{
    if (profileIndex < 0 || profileIndex >= m_profiles.count())
        return;
    if (m_doc->function(functionId) == nullptr)
        return;

    QVector<FunctionRef> &chasers = m_profiles[profileIndex].chasers;
    for (const FunctionRef &ref : chasers)
        if (!ref.isGroup && ref.id == functionId)
            return; // already present

    chasers.append(FunctionRef{ functionId, true, false });
    emit profilesChanged();
    emit visibleChasersChanged();
    syncMidiControls();
}

void VCMusicReactive::addGroupToProfile(int profileIndex, quint32 groupId)
{
    if (profileIndex < 0 || profileIndex >= m_profiles.count())
        return;
    if (groupIndexById(groupId) < 0)
        return;

    QVector<FunctionRef> &chasers = m_profiles[profileIndex].chasers;
    for (const FunctionRef &ref : chasers)
        if (ref.isGroup && ref.id == groupId)
            return; // already present

    chasers.append(FunctionRef{ groupId, true, true });
    emit profilesChanged();
    emit visibleChasersChanged();
    syncMidiControls();
}

void VCMusicReactive::removeChaserFromProfile(int profileIndex, int chaserIndex)
{
    if (profileIndex < 0 || profileIndex >= m_profiles.count())
        return;
    QVector<FunctionRef> &chasers = m_profiles[profileIndex].chasers;
    if (chaserIndex < 0 || chaserIndex >= chasers.count())
        return;

    if (profileIndex == m_currentProfileIndex)
        stopProfileEntry(chasers[chaserIndex]);

    chasers.remove(chaserIndex);
    emit profilesChanged();
    emit visibleChasersChanged();
    syncMidiControls();
}

void VCMusicReactive::setChaserEnabledInProfile(int profileIndex, int chaserIndex, bool enabled)
{
    if (profileIndex < 0 || profileIndex >= m_profiles.count())
        return;
    QVector<FunctionRef> &chasers = m_profiles[profileIndex].chasers;
    if (chaserIndex < 0 || chaserIndex >= chasers.count())
        return;

    if (chasers[chaserIndex].enabled == enabled)
        return;
    chasers[chaserIndex].enabled = enabled;

    QString key = QString("chaser:%1:%2:%3").arg(m_profiles[profileIndex].id)
                      .arg(chasers[chaserIndex].id).arg(refKindCode(chasers[chaserIndex]));
    if (m_midiControlKeyToId.contains(key))
        sendControlFeedback(m_midiControlKeyToId.value(key), enabled);

    // Live effect, right now, only if this Profile is the one actually
    // in control and not currently in a break (chasers are meant to be
    // stopped then regardless) - otherwise the flag just takes effect
    // whenever this Profile next becomes current/active resumes.
    if (profileIndex == m_currentProfileIndex && !m_inBreak)
    {
        if (enabled)
        {
            double masterIntensity = qBound(0.0, double(m_intensities[Master]) / 100.0, 1.0);
            advanceProfileEntry(chasers[chaserIndex], masterIntensity);
        }
        else
        {
            stopProfileEntry(chasers[chaserIndex]);
        }
    }

    emit visibleChasersChanged();
}

void VCMusicReactive::startOrStepChaser(quint32 functionId, double masterIntensity)
{
    m_startOrStepCalls++;

    Function *func = m_doc->function(functionId);
    if (func == nullptr)
        return;

    Chaser *ch = qobject_cast<Chaser*>(func);
    if (ch == nullptr)
    {
        // Not a Chaser - e.g. a "Super Chaser" (a Show, with its own
        // internal multi-track timeline) dropped into a Profile's Chaser
        // list. A Show has no beat-driven "step" concept of its own, so
        // there's nothing to advance here every Beat/Kick - just make sure
        // it's running once; its own timeline takes it from there, exactly
        // like a Show/any other Function dropped into an Idle Set already
        // does (see startAmbientEntry()) or a non-Chaser Group member
        // already does (see startGroupMember()'s own generic fallback).
        if (!func->isRunning())
            func->start(m_doc->masterTimer(), functionParent());
        return;
    }

    bool wasRunning = ch->isRunning();
    if (wasRunning)
        m_startOrStepAdvances++;
    else
        m_startOrStepFreshStarts++;
    qDebug() << "[VCMusicReactive] startOrStepChaser:" << ch->name() << "id" << functionId
             << "wasRunning" << wasRunning << "stepsCount" << ch->stepsCount()
             << "currentStepIndex" << ch->currentStepIndex();

    ChaserAction action;
    action.m_masterIntensity = masterIntensity;
    action.m_stepIntensity = 1.0;
    action.m_fadeMode = 0;

    if (wasRunning)
    {
        // Already running: ChaserNextStep, not an explicitly computed
        // ChaserSetStepIndex. An earlier version of this function did
        // exactly that (reading ch->currentStepIndex(), adding 1, wrapping)
        // on the theory that an explicit target is more robust than a
        // relative nudge across several simultaneously-driven Chasers -
        // built, installed and live-tested twice, and both times the
        // result was the SAME regression: chasers stopped advancing on
        // Beat/Kick entirely and only ever moved on their own configured
        // step duration, i.e. ChaserRunner::write()'s normal
        // getNextStepIndex()-driven fallback, never our explicit
        // ChaserSetStepIndex request. ChaserNextStep instead just clears
        // the running list and lets write() fall through to that exact
        // same getNextStepIndex()+startNewStep() path - the same code
        // that was demonstrably still working underneath (the "advances
        // every N seconds regardless of the widget" symptom is proof
        // getNextStepIndex()/startNewStep() themselves are fine) - so
        // it can't land on a state ChaserSetStepIndex was uniquely
        // failing on. See ChaserRunner::write()'s ChaserNextStep case.
        action.m_action = ChaserNextStep;
        ch->setAction(action);
    }
    else
    {
        // Not running yet: setAction() MUST be called before start() -
        // while ch->isRunning() is still false, Chaser::setAction()
        // stores the action into m_startupAction (there is no
        // ChaserRunner yet to hand it to), which start() then picks up
        // when it actually creates the runner. Calling start() first
        // races the asynchronous MasterTimer start and silently drops
        // the very first step - matches VCCueList::startChaser().
        action.m_action = ChaserSetStepIndex;
        action.m_stepIndex = 0;
        ch->setAction(action);
        ch->start(m_doc->masterTimer(), functionParent());
    }

    m_lastChaserStatus += QString("%1[%2->%3 st%4/%5] ")
                              .arg(ch->name())
                              .arg(wasRunning ? "run" : "START")
                              .arg(ch->isRunning() ? "run" : "stop")
                              .arg(ch->currentStepIndex())
                              .arg(ch->stepsCount());
}

void VCMusicReactive::advanceCurrentProfileChasers(double confidence)
{
    if (m_currentProfileIndex < 0 || m_currentProfileIndex >= m_profiles.count())
    {
        qDebug() << "[VCMusicReactive] advanceCurrentProfileChasers: no current profile";
        return;
    }

    double masterIntensity = qBound(0.0, double(m_intensities[Master]) / 100.0 * qBound(0.3, confidence, 1.0), 1.0);
    m_lastChaserStatus.clear();
    for (const FunctionRef &ref : m_profiles[m_currentProfileIndex].chasers)
    {
        if (ref.enabled)
            advanceProfileEntry(ref, masterIntensity);
        else
            m_lastChaserStatus += QString("%1[disabled] ").arg(functionOrGroupName(ref));
    }
    emit debugTextChanged();
}

void VCMusicReactive::startCurrentProfileChasers()
{
    // Reuses the exact same start-if-not-running logic as a normal
    // advance - a Profile becoming current is just its first "step".
    advanceCurrentProfileChasers(1.0);
}

void VCMusicReactive::stopCurrentProfileChasers()
{
    if (m_currentProfileIndex < 0 || m_currentProfileIndex >= m_profiles.count())
        return;

    for (const FunctionRef &ref : m_profiles[m_currentProfileIndex].chasers)
        stopProfileEntry(ref);
}

void VCMusicReactive::rotateToNextActiveProfile()
{
    if (m_profiles.isEmpty())
        return;

    // Round-robin starting just after whatever was current (or from the
    // top if nothing was), landing on the first active Profile found -
    // i.e. the very next one in list order, wrapping around.
    int start = (m_currentProfileIndex < 0) ? 0 : (m_currentProfileIndex + 1) % m_profiles.count();
    for (int i = 0; i < m_profiles.count(); i++)
    {
        int idx = (start + i) % m_profiles.count();
        if (m_profiles[idx].active)
        {
            m_currentProfileIndex = idx;
            startCurrentProfileChasers();
            emit visibleChasersChanged();
            return;
        }
    }
    // No active Profile at all
    m_currentProfileIndex = -1;
}

/*********************************************************************
 * Ambient sets - see the class comment in vcmusicreactive.h
 *********************************************************************/

QVariantList VCMusicReactive::visibleAmbientFunctions() const
{
    QVariantList list;
    for (int si = 0; si < m_ambientSets.count(); si++)
    {
        const AmbientSet &s = m_ambientSets[si];
        if (!s.active)
            continue;
        for (int fi = 0; fi < s.functions.count(); fi++)
        {
            const FunctionRef &ref = s.functions[fi];
            QVariantMap map;
            map.insert("id", ref.id);
            map.insert("name", functionOrGroupName(ref));
            map.insert("enabled", ref.enabled);
            map.insert("running", ref.enabled && si == m_currentAmbientSetIndex);
            map.insert("setIndex", si);
            map.insert("setName", s.name);
            map.insert("functionIndex", fi);
            map.insert("isGroup", ref.isGroup);
            map.insert("isShow", isShowFunction(ref));
            map.insert("isSuperChaser", ref.isSuperChaser);
            map.insert("midiControlId", midiControlIdFor(
                QString("idlefunc:%1:%2:%3").arg(s.id).arg(ref.id).arg(refKindCode(ref))));
            list << map;
        }
    }
    return list;
}

QVariantList VCMusicReactive::ambientSets() const
{
    QVariantList list;
    for (const AmbientSet &s : m_ambientSets)
    {
        QVariantMap map;
        map.insert("name", s.name);
        map.insert("active", s.active);
        map.insert("midiControlId", midiControlIdFor(QString("idleset:%1").arg(s.id)));

        QVariantList functions;
        for (const FunctionRef &ref : s.functions)
        {
            QVariantMap fmap;
            fmap.insert("id", ref.id);
            fmap.insert("name", functionOrGroupName(ref));
            fmap.insert("enabled", ref.enabled);
            fmap.insert("isGroup", ref.isGroup);
            fmap.insert("isShow", isShowFunction(ref));
            fmap.insert("isSuperChaser", ref.isSuperChaser);
            fmap.insert("midiControlId", midiControlIdFor(
                QString("idlefunc:%1:%2:%3").arg(s.id).arg(ref.id).arg(refKindCode(ref))));
            functions << fmap;
        }
        map.insert("functions", functions);

        list << map;
    }
    return list;
}

int VCMusicReactive::currentAmbientSetIndex() const
{
    return m_currentAmbientSetIndex;
}

void VCMusicReactive::addAmbientSet(const QString &name)
{
    AmbientSet s;
    s.id = m_nextAmbientSetId++;
    s.name = name.isEmpty() ? tr("Idle %1").arg(m_ambientSets.count() + 1) : name;
    m_ambientSets.append(s);
    emit ambientSetsChanged();
    syncMidiControls();
}

void VCMusicReactive::removeAmbientSetAt(int index)
{
    if (index < 0 || index >= m_ambientSets.count())
        return;

    if (index == m_currentAmbientSetIndex)
    {
        stopCurrentAmbientSetFunctions();
        m_currentAmbientSetIndex = -1;
    }
    else if (m_currentAmbientSetIndex > index)
    {
        m_currentAmbientSetIndex--;
    }

    m_ambientSets.remove(index);

    if (m_currentAmbientSetIndex < 0 && m_inBreak)
    {
        rotateToNextActiveAmbientSet();
        startCurrentAmbientSetFunctions();
    }

    emit ambientSetsChanged();
    emit ambientFunctionsChanged();
    syncMidiControls();
}

void VCMusicReactive::renameAmbientSet(int index, const QString &name)
{
    if (index < 0 || index >= m_ambientSets.count() || name.isEmpty())
        return;

    m_ambientSets[index].name = name;
    emit ambientSetsChanged();
    emit ambientFunctionsChanged(); // setName is embedded in each entry
    syncMidiControls();
}

void VCMusicReactive::setAmbientSetActive(int index, bool active)
{
    if (index < 0 || index >= m_ambientSets.count() || m_ambientSets[index].active == active)
        return;

    m_ambientSets[index].active = active;
    emit ambientSetsChanged();

    QString key = QString("idleset:%1").arg(m_ambientSets[index].id);
    if (m_midiControlKeyToId.contains(key))
        sendControlFeedback(m_midiControlKeyToId.value(key), active);

    // Only actually start/stop anything while a break is in progress -
    // same as before, Ambient Sets are silent the rest of the time
    // regardless of which ones are marked active, exactly like the old
    // flat Ambient list only ever ran while m_inBreak.
    if (active)
    {
        if (m_currentAmbientSetIndex < 0 && m_inBreak)
        {
            m_currentAmbientSetIndex = index;
            startCurrentAmbientSetFunctions();
            emit ambientSetsChanged();
        }
    }
    else if (index == m_currentAmbientSetIndex)
    {
        stopCurrentAmbientSetFunctions();
        m_currentAmbientSetIndex = -1;
        if (m_inBreak)
        {
            rotateToNextActiveAmbientSet();
            startCurrentAmbientSetFunctions();
        }
    }

    emit ambientFunctionsChanged();
}

void VCMusicReactive::addFunctionToAmbientSet(int setIndex, quint32 functionId)
{
    if (setIndex < 0 || setIndex >= m_ambientSets.count())
        return;
    if (m_doc->function(functionId) == nullptr)
        return;

    QVector<FunctionRef> &functions = m_ambientSets[setIndex].functions;
    for (const FunctionRef &ref : functions)
        if (!ref.isGroup && ref.id == functionId)
            return; // already present

    functions.append(FunctionRef{ functionId, true, false });
    emit ambientSetsChanged();
    emit ambientFunctionsChanged();
    syncMidiControls();
}

void VCMusicReactive::addGroupToAmbientSet(int setIndex, quint32 groupId)
{
    if (setIndex < 0 || setIndex >= m_ambientSets.count())
        return;
    if (groupIndexById(groupId) < 0)
        return;

    QVector<FunctionRef> &functions = m_ambientSets[setIndex].functions;
    for (const FunctionRef &ref : functions)
        if (ref.isGroup && ref.id == groupId)
            return; // already present

    functions.append(FunctionRef{ groupId, true, true });
    emit ambientSetsChanged();
    emit ambientFunctionsChanged();
    syncMidiControls();
}

void VCMusicReactive::removeFunctionFromAmbientSet(int setIndex, int functionIndex)
{
    if (setIndex < 0 || setIndex >= m_ambientSets.count())
        return;
    QVector<FunctionRef> &functions = m_ambientSets[setIndex].functions;
    if (functionIndex < 0 || functionIndex >= functions.count())
        return;

    if (setIndex == m_currentAmbientSetIndex)
        stopAmbientEntry(functions[functionIndex]);

    functions.remove(functionIndex);
    emit ambientSetsChanged();
    emit ambientFunctionsChanged();
    syncMidiControls();
}

void VCMusicReactive::setFunctionEnabledInAmbientSet(int setIndex, int functionIndex, bool enabled)
{
    if (setIndex < 0 || setIndex >= m_ambientSets.count())
        return;
    QVector<FunctionRef> &functions = m_ambientSets[setIndex].functions;
    if (functionIndex < 0 || functionIndex >= functions.count())
        return;

    if (functions[functionIndex].enabled == enabled)
        return;
    functions[functionIndex].enabled = enabled;

    QString key = QString("idlefunc:%1:%2:%3").arg(m_ambientSets[setIndex].id)
                      .arg(functions[functionIndex].id).arg(refKindCode(functions[functionIndex]));
    if (m_midiControlKeyToId.contains(key))
        sendControlFeedback(m_midiControlKeyToId.value(key), enabled);

    if (setIndex == m_currentAmbientSetIndex && m_inBreak)
    {
        if (enabled)
            startAmbientEntry(functions[functionIndex]);
        else
            stopAmbientEntry(functions[functionIndex]);
    }

    emit ambientSetsChanged();
    emit ambientFunctionsChanged();
}

void VCMusicReactive::startCurrentAmbientSetFunctions()
{
    if (m_currentAmbientSetIndex < 0 || m_currentAmbientSetIndex >= m_ambientSets.count())
        return;

    for (const FunctionRef &ref : m_ambientSets[m_currentAmbientSetIndex].functions)
    {
        if (ref.enabled)
            startAmbientEntry(ref);
    }
}

void VCMusicReactive::stopCurrentAmbientSetFunctions()
{
    if (m_currentAmbientSetIndex < 0 || m_currentAmbientSetIndex >= m_ambientSets.count())
        return;

    for (const FunctionRef &ref : m_ambientSets[m_currentAmbientSetIndex].functions)
        stopAmbientEntry(ref);
}

void VCMusicReactive::rotateToNextActiveAmbientSet()
{
    if (m_ambientSets.isEmpty())
    {
        m_currentAmbientSetIndex = -1;
        return;
    }

    // Same round-robin shape as rotateToNextActiveProfile() - the next
    // active Set in list order after whatever was current, wrapping
    // around, landing on the first one found. Does NOT start it - the
    // caller (applyBreakTransition()/removeAmbientSetAt()/
    // setAmbientSetActive()) decides whether that's appropriate right
    // now (only while m_inBreak).
    int start = (m_currentAmbientSetIndex < 0) ? 0 : (m_currentAmbientSetIndex + 1) % m_ambientSets.count();
    for (int i = 0; i < m_ambientSets.count(); i++)
    {
        int idx = (start + i) % m_ambientSets.count();
        if (m_ambientSets[idx].active)
        {
            m_currentAmbientSetIndex = idx;
            emit ambientSetsChanged();
            emit ambientFunctionsChanged();
            return;
        }
    }
    // No active Set at all
    m_currentAmbientSetIndex = -1;
}

/*********************************************************************
 * Groups - see the class comment in vcmusicreactive.h
 *********************************************************************/

int VCMusicReactive::groupIndexById(quint32 groupId) const
{
    for (int i = 0; i < m_groups.count(); i++)
        if (m_groups[i].id == groupId)
            return i;
    return -1;
}

QString VCMusicReactive::functionOrGroupName(const FunctionRef &ref) const
{
    if (ref.isSuperChaser)
    {
        int sci = superChaserIndexById(ref.id);
        return sci >= 0 ? m_superChasers[sci].name : tr("(missing super chaser)");
    }
    if (ref.isGroup)
    {
        int gi = groupIndexById(ref.id);
        return gi >= 0 ? m_groups[gi].name : tr("(missing group)");
    }
    Function *f = m_doc->function(ref.id);
    return f ? f->name() : tr("(missing)");
}

bool VCMusicReactive::isShowFunction(const FunctionRef &ref) const
{
    if (ref.isGroup || ref.isSuperChaser)
        return false;
    return qobject_cast<Show*>(m_doc->function(ref.id)) != nullptr;
}

int VCMusicReactive::superChaserIndexById(quint32 superChaserId) const
{
    for (int i = 0; i < m_superChasers.count(); i++)
        if (m_superChasers[i].id == superChaserId)
            return i;
    return -1;
}

bool VCMusicReactive::isSuperChaserFunction(const FunctionRef &ref) const
{
    return ref.isSuperChaser;
}

int VCMusicReactive::refKindCode(const FunctionRef &ref)
{
    if (ref.isSuperChaser)
        return 2;
    return ref.isGroup ? 1 : 0;
}

QVariantList VCMusicReactive::groups() const
{
    QVariantList list;
    for (const FunctionGroup &g : m_groups)
    {
        QVariantMap map;
        map.insert("id", g.id);
        map.insert("name", g.name);
        map.insert("rotate", g.rotate);
        map.insert("rotateIntervalSec", g.rotateIntervalSec);

        QVariantList members;
        for (quint32 fid : g.members)
        {
            QVariantMap mmap;
            mmap.insert("id", fid);
            Function *f = m_doc->function(fid);
            mmap.insert("name", f ? f->name() : tr("(missing)"));
            members << mmap;
        }
        map.insert("members", members);

        list << map;
    }
    return list;
}

quint32 VCMusicReactive::addGroup(const QString &name)
{
    FunctionGroup g;
    g.id = m_nextGroupId++;
    g.name = name.isEmpty() ? tr("Group %1").arg(m_groups.count() + 1) : name;
    m_groups.append(g);
    emit groupsChanged();
    return g.id;
}

void VCMusicReactive::removeGroupAt(int index)
{
    if (index < 0 || index >= m_groups.count())
        return;

    quint32 groupId = m_groups[index].id;

    // A Group being deleted outright is stopped wherever it's currently
    // actually running (as opposed to just removed from the Profile/
    // Ambient Set lists that reference it, which is a separate, later
    // step the properties panel doesn't do automatically - same as
    // deleting a Chaser/Scene out from under a Profile/Ambient Set
    // reference already silently drops it there on next load, this
    // just also makes sure nothing keeps outputting DMX right now).
    if (m_groupRuntime.contains(groupId))
    {
        const FunctionGroup &g = m_groups[index];
        const GroupRuntime &rt = m_groupRuntime[groupId];
        if (!g.rotate)
        {
            for (quint32 fid : g.members)
                stopGroupMember(fid);
        }
        else if (rt.currentMemberIndex >= 0 && rt.currentMemberIndex < g.members.count())
        {
            stopGroupMember(g.members[rt.currentMemberIndex]);
        }
        m_groupRuntime.remove(groupId);
    }

    m_groups.remove(index);
    emit groupsChanged();
    emit profilesChanged();
    emit visibleChasersChanged();
    emit ambientSetsChanged();
    emit ambientFunctionsChanged();
}

void VCMusicReactive::renameGroup(int index, const QString &name)
{
    if (index < 0 || index >= m_groups.count() || name.isEmpty())
        return;

    m_groups[index].name = name;
    emit groupsChanged();
    // The Group's name is embedded (via functionOrGroupName()) in every
    // Profile/Ambient Set entry that references it.
    emit profilesChanged();
    emit visibleChasersChanged();
    emit ambientSetsChanged();
    emit ambientFunctionsChanged();
}

void VCMusicReactive::setGroupRotate(int index, bool rotate)
{
    if (index < 0 || index >= m_groups.count() || m_groups[index].rotate == rotate)
        return;

    m_groups[index].rotate = rotate;
    // Switching mode while the Group happens to be running right now is
    // an edge case not worth trying to hot-swap cleanly - next time it
    // starts (Profile/Ambient Set becomes current again) it'll pick up
    // the new mode. Just drop any stale runtime state so a leftover
    // Rotate index doesn't linger under the new Parallel mode.
    m_groupRuntime.remove(m_groups[index].id);
    emit groupsChanged();
}

void VCMusicReactive::setGroupRotateIntervalSec(int index, int seconds)
{
    if (index < 0 || index >= m_groups.count() || seconds <= 0)
        return;

    m_groups[index].rotateIntervalSec = seconds;
    emit groupsChanged();
}

void VCMusicReactive::addMemberToGroup(int groupIndex, quint32 functionId)
{
    if (groupIndex < 0 || groupIndex >= m_groups.count())
        return;
    if (m_doc->function(functionId) == nullptr)
        return;

    QVector<quint32> &members = m_groups[groupIndex].members;
    if (members.contains(functionId))
        return;

    members.append(functionId);
    emit groupsChanged();
}

void VCMusicReactive::removeMemberFromGroup(int groupIndex, int memberIndex)
{
    if (groupIndex < 0 || groupIndex >= m_groups.count())
        return;
    QVector<quint32> &members = m_groups[groupIndex].members;
    if (memberIndex < 0 || memberIndex >= members.count())
        return;

    quint32 groupId = m_groups[groupIndex].id;
    quint32 removedId = members[memberIndex];

    // If this member happens to be the one currently selected by a
    // running Rotate instance, stop it and let tickGroupRotations() (or
    // the next natural start) sort out a valid index again.
    if (m_groupRuntime.contains(groupId))
    {
        GroupRuntime &rt = m_groupRuntime[groupId];
        if (rt.currentMemberIndex == memberIndex)
        {
            stopGroupMember(removedId);
            rt.currentMemberIndex = -1;
        }
        else if (rt.currentMemberIndex > memberIndex)
        {
            rt.currentMemberIndex--;
        }
    }

    members.remove(memberIndex);
    emit groupsChanged();
}

QVariantList VCMusicReactive::superChasers() const
{
    QVariantList list;
    for (const SuperChaser &sc : m_superChasers)
    {
        QVariantMap map;
        map.insert("id", sc.id);
        map.insert("name", sc.name);

        QVariantList tracks;
        for (const FunctionRef &ref : sc.tracks)
        {
            QVariantMap tmap;
            tmap.insert("id", ref.id);
            tmap.insert("name", functionOrGroupName(ref));
            tmap.insert("enabled", ref.enabled);
            tmap.insert("isGroup", ref.isGroup);
            tracks << tmap;
        }
        map.insert("tracks", tracks);

        list << map;
    }
    return list;
}

quint32 VCMusicReactive::addSuperChaser(const QString &name)
{
    SuperChaser sc;
    sc.id = m_nextSuperChaserId++;
    sc.name = name.isEmpty() ? tr("Super Chaser %1").arg(m_superChasers.count() + 1) : name;
    m_superChasers.append(sc);
    emit superChasersChanged();
    return sc.id;
}

void VCMusicReactive::removeSuperChaserAt(int index)
{
    if (index < 0 || index >= m_superChasers.count())
        return;

    m_superChasers.remove(index);
    emit superChasersChanged();
    syncMidiControls();
}

void VCMusicReactive::renameSuperChaser(int index, const QString &name)
{
    if (index < 0 || index >= m_superChasers.count() || name.isEmpty())
        return;

    m_superChasers[index].name = name;
    // the Super Chaser's name is embedded (via functionOrGroupName()) in
    // every Profile/Ambient Set entry that references it, and in its own
    // MIDI control label - refresh both.
    emit superChasersChanged();
    emit profilesChanged();
    emit visibleChasersChanged();
    emit ambientSetsChanged();
    emit ambientFunctionsChanged();
    syncMidiControls();
}

void VCMusicReactive::addTrackToSuperChaser(int superChaserIndex, quint32 functionId)
{
    if (superChaserIndex < 0 || superChaserIndex >= m_superChasers.count())
        return;
    if (m_doc->function(functionId) == nullptr)
        return;

    QVector<FunctionRef> &tracks = m_superChasers[superChaserIndex].tracks;
    for (const FunctionRef &ref : tracks)
        if (!ref.isGroup && !ref.isSuperChaser && ref.id == functionId)
            return; // already present

    tracks.append(FunctionRef{ functionId, true, false });
    emit superChasersChanged();
}

void VCMusicReactive::addGroupTrackToSuperChaser(int superChaserIndex, quint32 groupId)
{
    if (superChaserIndex < 0 || superChaserIndex >= m_superChasers.count())
        return;
    if (groupIndexById(groupId) < 0)
        return;

    QVector<FunctionRef> &tracks = m_superChasers[superChaserIndex].tracks;
    for (const FunctionRef &ref : tracks)
        if (ref.isGroup && ref.id == groupId)
            return; // already present

    tracks.append(FunctionRef{ groupId, true, true });
    emit superChasersChanged();
}

void VCMusicReactive::removeTrackFromSuperChaser(int superChaserIndex, int trackIndex)
{
    if (superChaserIndex < 0 || superChaserIndex >= m_superChasers.count())
        return;
    QVector<FunctionRef> &tracks = m_superChasers[superChaserIndex].tracks;
    if (trackIndex < 0 || trackIndex >= tracks.count())
        return;

    // If this Super Chaser happens to be running right now (referenced by
    // whatever Profile/Ambient Set is currently active), stop the track
    // being removed the same way removeChaserFromProfile() does - avoids
    // an orphaned Function left running with nothing left tracking it.
    stopProfileEntry(tracks[trackIndex]);

    tracks.remove(trackIndex);
    emit superChasersChanged();
}

void VCMusicReactive::setTrackEnabledInSuperChaser(int superChaserIndex, int trackIndex, bool enabled)
{
    if (superChaserIndex < 0 || superChaserIndex >= m_superChasers.count())
        return;
    QVector<FunctionRef> &tracks = m_superChasers[superChaserIndex].tracks;
    if (trackIndex < 0 || trackIndex >= tracks.count())
        return;

    tracks[trackIndex].enabled = enabled;
    if (!enabled)
        stopProfileEntry(tracks[trackIndex]);
    emit superChasersChanged();
}

void VCMusicReactive::addSuperChaserToProfile(int profileIndex, quint32 superChaserId)
{
    if (profileIndex < 0 || profileIndex >= m_profiles.count())
        return;
    if (superChaserIndexById(superChaserId) < 0)
        return;

    QVector<FunctionRef> &chasers = m_profiles[profileIndex].chasers;
    for (const FunctionRef &ref : chasers)
        if (ref.isSuperChaser && ref.id == superChaserId)
            return; // already present

    chasers.append(FunctionRef{ superChaserId, true, false, true });
    emit profilesChanged();
    emit visibleChasersChanged();
    syncMidiControls();
}

void VCMusicReactive::addSuperChaserToAmbientSet(int setIndex, quint32 superChaserId)
{
    if (setIndex < 0 || setIndex >= m_ambientSets.count())
        return;
    if (superChaserIndexById(superChaserId) < 0)
        return;

    QVector<FunctionRef> &functions = m_ambientSets[setIndex].functions;
    for (const FunctionRef &ref : functions)
        if (ref.isSuperChaser && ref.id == superChaserId)
            return; // already present

    functions.append(FunctionRef{ superChaserId, true, false, true });
    emit ambientSetsChanged();
    emit ambientFunctionsChanged();
    syncMidiControls();
}

/*********************************************************************
 * Super Shows - see the class comment in vcmusicreactive.h
 *********************************************************************/

QVariantList VCMusicReactive::superShows() const
{
    QVariantList list;
    for (const SuperShow &show : m_superShows)
    {
        QVariantMap map;
        map.insert("id", show.id);
        map.insert("name", show.name);
        map.insert("loop", show.loop);

        QVariantList sections;
        for (const SuperShowSection &sec : show.sections)
        {
            QVariantMap smap;
            smap.insert("profileId", sec.profileId);
            int pi = profileIndexById(sec.profileId);
            smap.insert("profileName", pi >= 0 ? m_profiles[pi].name : tr("(missing profile)"));
            smap.insert("durationSec", sec.durationSec);
            sections << smap;
        }
        map.insert("sections", sections);

        list << map;
    }
    return list;
}

quint32 VCMusicReactive::addSuperShow(const QString &name)
{
    SuperShow show;
    show.id = m_nextSuperShowId++;
    show.name = name.isEmpty() ? tr("Super Show %1").arg(m_superShows.count() + 1) : name;
    m_superShows.append(show);
    emit superShowsChanged();
    return show.id;
}

void VCMusicReactive::removeSuperShowAt(int index)
{
    if (index < 0 || index >= m_superShows.count())
        return;

    if (index == m_activeSuperShowIndex)
        stopSuperShow();
    else if (m_activeSuperShowIndex > index)
        m_activeSuperShowIndex--;

    m_superShows.remove(index);
    emit superShowsChanged();
}

void VCMusicReactive::renameSuperShow(int index, const QString &name)
{
    if (index < 0 || index >= m_superShows.count() || name.isEmpty())
        return;

    m_superShows[index].name = name;
    emit superShowsChanged();
}

void VCMusicReactive::setSuperShowLoop(int index, bool loop)
{
    if (index < 0 || index >= m_superShows.count())
        return;

    m_superShows[index].loop = loop;
    emit superShowsChanged();
}

void VCMusicReactive::addSectionToSuperShow(int showIndex, quint32 profileId, int durationSec)
{
    if (showIndex < 0 || showIndex >= m_superShows.count())
        return;
    if (profileIndexById(profileId) < 0)
        return;

    SuperShowSection sec;
    sec.profileId = profileId;
    sec.durationSec = qMax(1, durationSec);
    m_superShows[showIndex].sections.append(sec);
    emit superShowsChanged();
}

void VCMusicReactive::removeSectionFromSuperShow(int showIndex, int sectionIndex)
{
    if (showIndex < 0 || showIndex >= m_superShows.count())
        return;
    QVector<SuperShowSection> &sections = m_superShows[showIndex].sections;
    if (sectionIndex < 0 || sectionIndex >= sections.count())
        return;

    sections.remove(sectionIndex);
    emit superShowsChanged();

    // The section the playhead was in may no longer exist / may now mean
    // something else positionally - simplest correct thing is to just
    // stop; restarting is one click away and silently mis-attributing
    // "elapsed time" to the wrong remaining section would be worse.
    if (showIndex == m_activeSuperShowIndex)
        stopSuperShow();
}

void VCMusicReactive::setSectionDuration(int showIndex, int sectionIndex, int durationSec)
{
    if (showIndex < 0 || showIndex >= m_superShows.count())
        return;
    QVector<SuperShowSection> &sections = m_superShows[showIndex].sections;
    if (sectionIndex < 0 || sectionIndex >= sections.count())
        return;

    sections[sectionIndex].durationSec = qMax(1, durationSec);
    emit superShowsChanged();
}

void VCMusicReactive::setSectionProfile(int showIndex, int sectionIndex, quint32 profileId)
{
    if (showIndex < 0 || showIndex >= m_superShows.count())
        return;
    QVector<SuperShowSection> &sections = m_superShows[showIndex].sections;
    if (sectionIndex < 0 || sectionIndex >= sections.count())
        return;
    if (profileIndexById(profileId) < 0)
        return;

    sections[sectionIndex].profileId = profileId;
    emit superShowsChanged();
}

void VCMusicReactive::startSuperShow(int index)
{
    if (index < 0 || index >= m_superShows.count() || m_superShows[index].sections.isEmpty())
        return;

    if (m_activeSuperShowIndex >= 0 && m_activeSuperShowIndex != index)
        stopSuperShow();

    m_activeSuperShowIndex = index;
    m_activeSuperShowSectionIndex = -1; // force tickSuperShow() to apply section 0 below
    m_superShowTimer.start();
    m_superShowTimerValid = true;

    tickSuperShow(); // apply section 0 immediately, don't wait for the next audio block
}

void VCMusicReactive::stopSuperShow()
{
    if (m_activeSuperShowIndex < 0)
        return;

    m_activeSuperShowIndex = -1;
    m_activeSuperShowSectionIndex = -1;
    m_superShowTimerValid = false;
    emit superShowPlaybackChanged();
}

int VCMusicReactive::activeSuperShowIndex() const
{
    return m_activeSuperShowIndex;
}

int VCMusicReactive::activeSuperShowSectionIndex() const
{
    return m_activeSuperShowSectionIndex;
}

qint64 VCMusicReactive::superShowElapsedMs() const
{
    if (!m_superShowTimerValid)
        return 0;
    return m_superShowTimer.elapsed();
}

void VCMusicReactive::tickSuperShow()
{
    if (m_activeSuperShowIndex < 0 || m_activeSuperShowIndex >= m_superShows.count())
        return;

    const SuperShow &show = m_superShows[m_activeSuperShowIndex];
    if (show.sections.isEmpty() || !m_superShowTimerValid)
        return;

    qint64 totalMs = 0;
    for (const SuperShowSection &sec : show.sections)
        totalMs += qint64(sec.durationSec) * 1000;
    if (totalMs <= 0)
        return;

    qint64 elapsed = m_superShowTimer.elapsed();
    if (elapsed >= totalMs)
    {
        if (!show.loop)
        {
            stopSuperShow();
            return;
        }
        elapsed %= totalMs;
    }

    int sectionIdx = show.sections.count() - 1; // fallback: last section
    qint64 cursor = 0;
    for (int i = 0; i < show.sections.count(); i++)
    {
        qint64 secMs = qint64(show.sections[i].durationSec) * 1000;
        if (elapsed < cursor + secMs)
        {
            sectionIdx = i;
            break;
        }
        cursor += secMs;
    }

    if (sectionIdx != m_activeSuperShowSectionIndex)
    {
        m_activeSuperShowSectionIndex = sectionIdx;

        int newProfileIdx = profileIndexById(show.sections[sectionIdx].profileId);
        if (newProfileIdx >= 0 && newProfileIdx != m_currentProfileIndex)
        {
            stopCurrentProfileChasers();
            m_currentProfileIndex = newProfileIdx;
            startCurrentProfileChasers();
            emit visibleChasersChanged();
        }
    }

    emit superShowPlaybackChanged();
}

void VCMusicReactive::startGroupMember(quint32 functionId, double masterIntensity)
{
    Function *f = m_doc->function(functionId);
    if (f == nullptr)
        return;

    Chaser *ch = qobject_cast<Chaser*>(f);
    if (ch != nullptr)
    {
        // Fresh start at step 0 - same shape as startOrStepChaser()'s
        // "not running" branch, see the comment there for why setAction()
        // has to be called before start().
        ChaserAction action;
        action.m_action = ChaserSetStepIndex;
        action.m_stepIndex = 0;
        action.m_masterIntensity = masterIntensity;
        action.m_stepIntensity = 1.0;
        action.m_fadeMode = 0;
        ch->setAction(action);
        ch->start(m_doc->masterTimer(), functionParent());
    }
    else if (!f->isRunning())
    {
        f->start(m_doc->masterTimer(), functionParent());
    }
}

void VCMusicReactive::stopGroupMember(quint32 functionId)
{
    Function *f = m_doc->function(functionId);
    if (f != nullptr && f->isRunning())
        f->stop(functionParent());
}

void VCMusicReactive::advanceProfileEntry(const FunctionRef &ref, double masterIntensity)
{
    if (ref.isSuperChaser)
    {
        // A Super Chaser is just a bundle of tracks (plain Chasers/Scenes
        // or Groups) that all run in parallel - recurse into every
        // enabled one with this exact same function, so each track's
        // Chaser advances via the identical startOrStepChaser() call (and
        // therefore the identical Beat/Kick timing) as a Chaser placed
        // directly in the Profile. See the class comment above
        // superChasers() for why this isn't built on Show/Track instead.
        int sci = superChaserIndexById(ref.id);
        if (sci < 0)
            return;
        for (const FunctionRef &track : m_superChasers[sci].tracks)
            if (track.enabled)
                advanceProfileEntry(track, masterIntensity);
        return;
    }

    if (!ref.isGroup)
    {
        startOrStepChaser(ref.id, masterIntensity);
        return;
    }

    int gi = groupIndexById(ref.id);
    if (gi < 0)
        return;
    const FunctionGroup &g = m_groups[gi];

    if (!g.rotate)
    {
        // Parallel: every member behaves exactly like a plain Profile
        // Chaser entry - steps forward on every Beat/Kick same as always.
        for (quint32 memberId : g.members)
            startOrStepChaser(memberId, masterIntensity);
    }
    else
    {
        // Rotate: WHICH member is current is entirely tickGroupRotations()'s
        // job (wall-clock timed, not Beat/Kick-driven) - this just needs
        // to keep whichever one is already selected stepping on the beat.
        // If nothing is selected yet (Group just started), kick off member
        // 0 the same way startAmbientEntry() does for a fresh Rotate start.
        GroupRuntime &rt = m_groupRuntime[ref.id];
        if (rt.currentMemberIndex < 0 && !g.members.isEmpty())
        {
            rt.currentMemberIndex = 0;
            rt.timer.start();
            rt.timerValid = true;
            startGroupMember(g.members[0], masterIntensity);
        }
        else if (rt.currentMemberIndex >= 0 && rt.currentMemberIndex < g.members.count())
        {
            startOrStepChaser(g.members[rt.currentMemberIndex], masterIntensity);
        }
    }
}

void VCMusicReactive::stopProfileEntry(const FunctionRef &ref)
{
    if (ref.isSuperChaser)
    {
        int sci = superChaserIndexById(ref.id);
        if (sci < 0)
            return;
        for (const FunctionRef &track : m_superChasers[sci].tracks)
            stopProfileEntry(track);
        return;
    }

    if (!ref.isGroup)
    {
        stopGroupMember(ref.id); // works for any Function, not Chaser-specific
        return;
    }

    int gi = groupIndexById(ref.id);
    if (gi < 0)
        return;
    const FunctionGroup &g = m_groups[gi];

    if (!g.rotate)
    {
        for (quint32 memberId : g.members)
            stopGroupMember(memberId);
    }
    else if (m_groupRuntime.contains(ref.id))
    {
        GroupRuntime &rt = m_groupRuntime[ref.id];
        if (rt.currentMemberIndex >= 0 && rt.currentMemberIndex < g.members.count())
            stopGroupMember(g.members[rt.currentMemberIndex]);
    }
    m_groupRuntime.remove(ref.id);
}

void VCMusicReactive::startAmbientEntry(const FunctionRef &ref)
{
    if (ref.isSuperChaser)
    {
        int sci = superChaserIndexById(ref.id);
        if (sci < 0)
            return;
        for (const FunctionRef &track : m_superChasers[sci].tracks)
            if (track.enabled)
                startAmbientEntry(track);
        return;
    }

    if (!ref.isGroup)
    {
        Function *f = m_doc->function(ref.id);
        if (f != nullptr && !f->isRunning())
            f->start(m_doc->masterTimer(), functionParent());
        return;
    }

    int gi = groupIndexById(ref.id);
    if (gi < 0)
        return;
    const FunctionGroup &g = m_groups[gi];

    if (!g.rotate)
    {
        for (quint32 memberId : g.members)
        {
            Function *f = m_doc->function(memberId);
            if (f != nullptr && !f->isRunning())
                f->start(m_doc->masterTimer(), functionParent());
        }
    }
    else
    {
        // Every fresh start begins at member 0, regardless of where a
        // previous run of this Group left off - see the class comment
        // above groups() for why rotation state isn't persisted.
        GroupRuntime &rt = m_groupRuntime[ref.id];
        rt.currentMemberIndex = g.members.isEmpty() ? -1 : 0;
        rt.timer.start();
        rt.timerValid = true;
        if (rt.currentMemberIndex >= 0)
            startGroupMember(g.members[0], 1.0);
    }
}

void VCMusicReactive::stopAmbientEntry(const FunctionRef &ref)
{
    if (ref.isSuperChaser)
    {
        int sci = superChaserIndexById(ref.id);
        if (sci < 0)
            return;
        for (const FunctionRef &track : m_superChasers[sci].tracks)
            stopAmbientEntry(track);
        return;
    }

    if (!ref.isGroup)
    {
        Function *f = m_doc->function(ref.id);
        if (f != nullptr && f->isRunning())
            f->stop(functionParent());
        return;
    }

    int gi = groupIndexById(ref.id);
    if (gi < 0)
    {
        m_groupRuntime.remove(ref.id);
        return;
    }
    const FunctionGroup &g = m_groups[gi];

    if (!g.rotate)
    {
        for (quint32 memberId : g.members)
        {
            Function *f = m_doc->function(memberId);
            if (f != nullptr && f->isRunning())
                f->stop(functionParent());
        }
    }
    else if (m_groupRuntime.contains(ref.id))
    {
        const GroupRuntime &rt = m_groupRuntime[ref.id];
        if (rt.currentMemberIndex >= 0 && rt.currentMemberIndex < g.members.count())
        {
            Function *f = m_doc->function(g.members[rt.currentMemberIndex]);
            if (f != nullptr && f->isRunning())
                f->stop(functionParent());
        }
    }
    m_groupRuntime.remove(ref.id);
}

void VCMusicReactive::tickGroupRotations()
{
    if (m_groupRuntime.isEmpty())
        return;

    // Snapshot the keys: rotating a Group can, in principle, touch
    // m_groupRuntime for OTHER groups only via stop/start on plain
    // Functions, never inserts/removes further entries mid-loop here,
    // but iterating a QHash while indexing by key (not by iterator) is
    // the safe, simple way to avoid any doubt about that.
    const QList<quint32> groupIds = m_groupRuntime.keys();
    for (quint32 groupId : groupIds)
    {
        GroupRuntime &rt = m_groupRuntime[groupId];
        if (!rt.timerValid)
            continue;

        int gi = groupIndexById(groupId);
        if (gi < 0)
        {
            m_groupRuntime.remove(groupId);
            continue;
        }
        const FunctionGroup &g = m_groups[gi];
        if (!g.rotate || g.members.isEmpty())
            continue;

        if (rt.timer.elapsed() < qint64(g.rotateIntervalSec) * 1000)
            continue;

        if (rt.currentMemberIndex >= 0 && rt.currentMemberIndex < g.members.count())
            stopGroupMember(g.members[rt.currentMemberIndex]);

        rt.currentMemberIndex = (rt.currentMemberIndex + 1) % g.members.count();
        // Master intensity here mirrors the modest, non-confidence-scaled
        // level ambient/idle already runs its functions at - see
        // startAmbientEntry(). A Rotate Group inside a Profile's own
        // beat-driven advance (advanceProfileEntry()) re-applies its own
        // proper masterIntensity on the very next Beat/Kick anyway, so
        // this is only the level it briefly starts at right at the
        // switch itself.
        startGroupMember(g.members[rt.currentMemberIndex], 1.0);
        rt.timer.restart();
    }
}

/*********************************************************************
 * MIDI/external control - see the class comment in vcmusicreactive.h
 *********************************************************************/

bool VCMusicReactive::midiEditMode() const
{
    return m_midiEditMode;
}

void VCMusicReactive::setMidiEditMode(bool enable)
{
    if (enable == m_midiEditMode)
        return;

    m_midiEditMode = enable;
    emit midiEditModeChanged();
}

void VCMusicReactive::learnMidiForControl(int controlId)
{
    if (controlId < 0 || controlId > 254 || m_vc == nullptr)
        return;

    // Forget any existing mapping for this control first, so re-learning
    // replaces it instead of leaving the old one active alongside a new
    // one - deleteInputSurce() is a no-op if none exists yet.
    for (const QSharedPointer<QLCInputSource> &src : inputSources())
    {
        if (src->id() == quint32(controlId))
        {
            deleteInputSurce(quint32(controlId), src->universe(), src->channel());
            break;
        }
    }

    QSharedPointer<QLCInputSource> source(new QLCInputSource());
    source->setID(quint32(controlId));
    addInputSource(source);

    m_vc->enableInputSourceAutoDetection(this, quint32(controlId),
                                          QLCInputSource::invalidUniverse, QLCInputSource::invalidChannel);
}

void VCMusicReactive::slotInputValueChanged(quint8 id, uchar value)
{
    // A toggle, not a momentary/fader control - only react to the
    // "pressed"/"on" edge (matches how a MIDI pad's note-on is what
    // should flip a checkbox, not its note-off release) and flip
    // whatever this control id currently maps to. Reverse-lookup by
    // scanning m_midiControlKeyToId - only happens on an actual MIDI/
    // keyboard event, nowhere near a hot path.
    if (value == 0)
        return;

    QString key;
    for (auto it = m_midiControlKeyToId.constBegin(); it != m_midiControlKeyToId.constEnd(); ++it)
    {
        if (it.value() == id)
        {
            key = it.key();
            break;
        }
    }
    if (key.isEmpty())
        return;

    QStringList parts = key.split(':');
    if (parts.isEmpty())
        return;

    if (parts.first() == "profile" && parts.count() == 2)
    {
        quint32 profileId = parts.at(1).toUInt();
        for (int i = 0; i < m_profiles.count(); i++)
        {
            if (m_profiles[i].id == profileId)
            {
                setProfileActive(i, !m_profiles[i].active);
                return;
            }
        }
    }
    else if (parts.first() == "chaser" && parts.count() == 4)
    {
        quint32 profileId = parts.at(1).toUInt();
        quint32 refId = parts.at(2).toUInt();
        bool isGroup = parts.at(3) == "1";
        for (int pi = 0; pi < m_profiles.count(); pi++)
        {
            if (m_profiles[pi].id != profileId)
                continue;
            const QVector<FunctionRef> &chasers = m_profiles[pi].chasers;
            for (int ci = 0; ci < chasers.count(); ci++)
            {
                if (chasers[ci].id == refId && chasers[ci].isGroup == isGroup)
                {
                    setChaserEnabledInProfile(pi, ci, !chasers[ci].enabled);
                    return;
                }
            }
        }
    }
    else if (parts.first() == "idleset" && parts.count() == 2)
    {
        quint32 setId = parts.at(1).toUInt();
        for (int i = 0; i < m_ambientSets.count(); i++)
        {
            if (m_ambientSets[i].id == setId)
            {
                setAmbientSetActive(i, !m_ambientSets[i].active);
                return;
            }
        }
    }
    else if (parts.first() == "idlefunc" && parts.count() == 4)
    {
        quint32 setId = parts.at(1).toUInt();
        quint32 refId = parts.at(2).toUInt();
        bool isGroup = parts.at(3) == "1";
        for (int si = 0; si < m_ambientSets.count(); si++)
        {
            if (m_ambientSets[si].id != setId)
                continue;
            const QVector<FunctionRef> &functions = m_ambientSets[si].functions;
            for (int fi = 0; fi < functions.count(); fi++)
            {
                if (functions[fi].id == refId && functions[fi].isGroup == isGroup)
                {
                    setFunctionEnabledInAmbientSet(si, fi, !functions[fi].enabled);
                    return;
                }
            }
        }
    }
}

void VCMusicReactive::sendControlFeedback(quint8 controlId, bool on)
{
    sendFeedback(on ? UCHAR_MAX : 0, controlId, on ? UpperValue : LowerValue);
}

void VCMusicReactive::updateFeedback()
{
    for (auto it = m_midiControlKeyToId.constBegin(); it != m_midiControlKeyToId.constEnd(); ++it)
    {
        const QString &key = it.key();
        QStringList parts = key.split(':');
        bool on = false;

        if (parts.first() == "profile" && parts.count() == 2)
        {
            quint32 profileId = parts.at(1).toUInt();
            for (const MusicProfile &p : m_profiles)
                if (p.id == profileId) { on = p.active; break; }
        }
        else if (parts.first() == "chaser" && parts.count() == 4)
        {
            quint32 profileId = parts.at(1).toUInt();
            quint32 refId = parts.at(2).toUInt();
            bool isGroup = parts.at(3) == "1";
            for (const MusicProfile &p : m_profiles)
            {
                if (p.id != profileId)
                    continue;
                for (const FunctionRef &ref : p.chasers)
                    if (ref.id == refId && ref.isGroup == isGroup) { on = ref.enabled; break; }
                break;
            }
        }
        else if (parts.first() == "idleset" && parts.count() == 2)
        {
            quint32 setId = parts.at(1).toUInt();
            for (const AmbientSet &s : m_ambientSets)
                if (s.id == setId) { on = s.active; break; }
        }
        else if (parts.first() == "idlefunc" && parts.count() == 4)
        {
            quint32 setId = parts.at(1).toUInt();
            quint32 refId = parts.at(2).toUInt();
            bool isGroup = parts.at(3) == "1";
            for (const AmbientSet &s : m_ambientSets)
            {
                if (s.id != setId)
                    continue;
                for (const FunctionRef &ref : s.functions)
                    if (ref.id == refId && ref.isGroup == isGroup) { on = ref.enabled; break; }
                break;
            }
        }

        sendControlFeedback(it.value(), on);
    }
}

int VCMusicReactive::midiControlIdFor(const QString &key) const
{
    return m_midiControlKeyToId.contains(key) ? int(m_midiControlKeyToId.value(key)) : -1;
}

void VCMusicReactive::syncMidiControls()
{
    // Desired control set, right now - key -> label.
    QHash<QString, QString> desired;

    for (const MusicProfile &p : m_profiles)
    {
        desired.insert(QString("profile:%1").arg(p.id), tr("Profile: %1").arg(p.name));
        for (const FunctionRef &ref : p.chasers)
        {
            QString key = QString("chaser:%1:%2:%3").arg(p.id).arg(ref.id).arg(refKindCode(ref));
            desired.insert(key, tr("%1 (%2)").arg(functionOrGroupName(ref), p.name));
        }
    }
    for (const AmbientSet &s : m_ambientSets)
    {
        desired.insert(QString("idleset:%1").arg(s.id), tr("Idle: %1").arg(s.name));
        for (const FunctionRef &ref : s.functions)
        {
            QString key = QString("idlefunc:%1:%2:%3").arg(s.id).arg(ref.id).arg(refKindCode(ref));
            desired.insert(key, tr("%1 (%2)").arg(functionOrGroupName(ref), s.name));
        }
    }

    // Unregister whatever's no longer wanted.
    QStringList staleKeys;
    for (auto it = m_midiControlKeyToId.constBegin(); it != m_midiControlKeyToId.constEnd(); ++it)
        if (!desired.contains(it.key()))
            staleKeys.append(it.key());
    for (const QString &key : staleKeys)
    {
        unregisterExternalControl(m_midiControlKeyToId.value(key));
        m_midiControlKeyToId.remove(key);
    }

    // Register/refresh everything currently wanted.
    for (auto it = desired.constBegin(); it != desired.constEnd(); ++it)
    {
        quint8 id;
        if (m_midiControlKeyToId.contains(it.key()))
        {
            id = m_midiControlKeyToId.value(it.key());
        }
        else
        {
            if (m_nextMidiControlId > 254)
            {
                qWarning() << "[VCMusicReactive] syncMidiControls: out of MIDI control ids (255 max) - dropping"
                           << it.key();
                continue;
            }
            id = quint8(m_nextMidiControlId++);
            m_midiControlKeyToId.insert(it.key(), id);
        }
        registerExternalControl(id, it.value(), true);
    }
}

/*********************************************************************
 * Load & Save
 *********************************************************************/

bool VCMusicReactive::loadXML(QXmlStreamReader &root)
{
    qDebug() << "[VCMusicReactive] loadXML starting, node name:" << root.name().toString();

    if (root.name() != KXMLQLCVCMusicReactive)
    {
        qWarning() << Q_FUNC_INFO << "Music Reactive node not found";
        return false;
    }

    loadXMLCommon(root);

    while (root.readNextStartElement())
    {
        qDebug() << "[VCMusicReactive] loadXML: child tag" << root.name().toString();
        if (root.name() == KXMLQLCWindowState)
        {
            bool visible = false;
            int x = 0, y = 0, w = 0, h = 0;
            loadXMLWindowState(root, &x, &y, &w, &h, &visible);
            setGeometry(QRect(x, y, w, h));
        }
        else if (root.name() == KXMLQLCVCWidgetAppearance)
        {
            loadXMLAppearance(root);
        }
        else if (root.name() == KXMLQLCVCMusicReactiveIntensities)
        {
            QString txt = root.readElementText();
            QStringList parts = txt.split(",");
            for (int i = 0; i < BandCount && i < parts.count(); i++)
                m_intensities[i] = qBound(0, parts.at(i).toInt(), 100);
        }
        else if (root.name() == KXMLQLCVCMusicReactiveThresholds)
        {
            QString txt = root.readElementText();
            QStringList parts = txt.split(",");
            for (int i = 0; i < BandCount && i < parts.count(); i++)
                m_thresholds[i] = qBound(0, parts.at(i).toInt(), 100);
        }
        else if (root.name() == KXMLQLCVCMusicReactiveAutoMode)
        {
            m_autoMode = root.readElementText() != "false";
        }
        else if (root.name() == KXMLQLCVCMusicReactiveAdvanceOnBeat)
        {
            m_advanceOnBeat = root.readElementText() == "true";
        }
        else if (root.name() == KXMLQLCVCMusicReactiveProfile)
        {
            QXmlStreamAttributes attrs = root.attributes();
            MusicProfile p;
            p.id = attrs.hasAttribute(KXMLQLCVCMusicReactiveGroupId)
                 ? attrs.value(KXMLQLCVCMusicReactiveGroupId).toString().toUInt() : 0;
            p.name = attrs.value(KXMLQLCVCMusicReactiveProfileName).toString();
            if (p.name.isEmpty())
                p.name = tr("Profile %1").arg(m_profiles.count() + 1);
            p.active = attrs.hasAttribute(KXMLQLCVCMusicReactiveActive)
                    && attrs.value(KXMLQLCVCMusicReactiveActive).toString() == "true";

            while (root.readNextStartElement())
            {
                if (root.name() == KXMLQLCVCMusicReactiveProfileChaser)
                {
                    QXmlStreamAttributes cattrs = root.attributes();
                    quint32 fid = cattrs.value(KXMLQLCVCMusicReactiveFunctionID).toString().toUInt();
                    bool enabled = !cattrs.hasAttribute(KXMLQLCVCMusicReactiveEnabled)
                                || cattrs.value(KXMLQLCVCMusicReactiveEnabled).toString() == "true";
                    bool isGroup = cattrs.hasAttribute(KXMLQLCVCMusicReactiveIsGroup)
                                && cattrs.value(KXMLQLCVCMusicReactiveIsGroup).toString() == "true";
                    bool isSuperChaser = cattrs.hasAttribute(KXMLQLCVCMusicReactiveIsSuperChaser)
                                && cattrs.value(KXMLQLCVCMusicReactiveIsSuperChaser).toString() == "true";
                    // A Group/Super Chaser id can't be validated here -
                    // those elements may not have been parsed yet (order
                    // within <MusicReactive> isn't guaranteed) - any
                    // dangling reference is handled gracefully at
                    // runtime instead (groupIndexById()/
                    // superChaserIndexById() returning -1).
                    if (!isGroup && !isSuperChaser && m_doc->function(fid) == nullptr)
                        qWarning() << "[VCMusicReactive] loadXML: profile chaser function id" << fid
                                   << "does not exist (any more) - dropping this entry";
                    else
                        p.chasers.append(FunctionRef{ fid, enabled, isGroup, isSuperChaser });
                    root.skipCurrentElement();
                }
                else
                {
                    root.skipCurrentElement();
                }
            }

            m_profiles.append(p);
        }
        else if (root.name() == KXMLQLCVCMusicReactiveChaser)
        {
            // Legacy (pre-Profiles) format: a flat list of Chasers, at
            // most one marked "active". Fold them all into a single
            // implicit "Profile 1" (created on first encounter) instead
            // of dropping them, so an existing project's setup survives
            // opening it after this update - the old "active" one just
            // becomes the first enabled entry, same practical effect
            // (it's what actually ran before).
            QXmlStreamAttributes attrs = root.attributes();
            quint32 fid = attrs.value(KXMLQLCVCMusicReactiveFunctionID).toString().toUInt();
            if (m_doc->function(fid) == nullptr)
            {
                qWarning() << "[VCMusicReactive] loadXML: legacy chaser function id" << fid
                           << "does not exist (any more) - dropping this entry";
            }
            else
            {
                int legacyIndex = -1;
                for (int i = 0; i < m_profiles.count(); i++)
                    if (m_profiles[i].name == tr("Profile 1")) { legacyIndex = i; break; }
                if (legacyIndex < 0)
                {
                    MusicProfile p;
                    p.name = tr("Profile 1");
                    p.active = true;
                    m_profiles.append(p);
                    legacyIndex = m_profiles.count() - 1;
                }
                m_profiles[legacyIndex].chasers.append(FunctionRef{ fid, true, false });
            }
            root.skipCurrentElement();
        }
        else if (root.name() == KXMLQLCVCMusicReactiveAmbient)
        {
            // Legacy (pre-Ambient-Sets) format: a flat list of
            // functions, all running together during a break with no
            // variety. Same migration shape as the legacy flat Chaser
            // format above - fold them all into a single implicit
            // "Idle 1" Set (created on first encounter, marked active)
            // instead of dropping them.
            QXmlStreamAttributes attrs = root.attributes();
            quint32 fid = attrs.value(KXMLQLCVCMusicReactiveFunctionID).toString().toUInt();
            bool enabled = !attrs.hasAttribute(KXMLQLCVCMusicReactiveEnabled)
                        || attrs.value(KXMLQLCVCMusicReactiveEnabled).toString() == "true";
            if (m_doc->function(fid) == nullptr)
            {
                qWarning() << "[VCMusicReactive] loadXML: ambient function id" << fid
                           << "does not exist (any more) - dropping this entry";
            }
            else
            {
                int legacyIndex = -1;
                for (int i = 0; i < m_ambientSets.count(); i++)
                    if (m_ambientSets[i].name == tr("Idle 1")) { legacyIndex = i; break; }
                if (legacyIndex < 0)
                {
                    AmbientSet s;
                    s.name = tr("Idle 1");
                    s.active = true;
                    m_ambientSets.append(s);
                    legacyIndex = m_ambientSets.count() - 1;
                }
                m_ambientSets[legacyIndex].functions.append(FunctionRef{ fid, enabled, false });
            }
            root.skipCurrentElement();
        }
        else if (root.name() == KXMLQLCVCMusicReactiveAmbientSet)
        {
            QXmlStreamAttributes attrs = root.attributes();
            AmbientSet s;
            s.id = attrs.hasAttribute(KXMLQLCVCMusicReactiveGroupId)
                 ? attrs.value(KXMLQLCVCMusicReactiveGroupId).toString().toUInt() : 0;
            s.name = attrs.value(KXMLQLCVCMusicReactiveProfileName).toString();
            if (s.name.isEmpty())
                s.name = tr("Idle %1").arg(m_ambientSets.count() + 1);
            s.active = attrs.hasAttribute(KXMLQLCVCMusicReactiveActive)
                    && attrs.value(KXMLQLCVCMusicReactiveActive).toString() == "true";

            while (root.readNextStartElement())
            {
                if (root.name() == KXMLQLCVCMusicReactiveAmbientSetFunction)
                {
                    QXmlStreamAttributes fattrs = root.attributes();
                    quint32 fid = fattrs.value(KXMLQLCVCMusicReactiveFunctionID).toString().toUInt();
                    bool enabled = !fattrs.hasAttribute(KXMLQLCVCMusicReactiveEnabled)
                                || fattrs.value(KXMLQLCVCMusicReactiveEnabled).toString() == "true";
                    bool isGroup = fattrs.hasAttribute(KXMLQLCVCMusicReactiveIsGroup)
                                && fattrs.value(KXMLQLCVCMusicReactiveIsGroup).toString() == "true";
                    bool isSuperChaser = fattrs.hasAttribute(KXMLQLCVCMusicReactiveIsSuperChaser)
                                && fattrs.value(KXMLQLCVCMusicReactiveIsSuperChaser).toString() == "true";
                    if (!isGroup && !isSuperChaser && m_doc->function(fid) == nullptr)
                        qWarning() << "[VCMusicReactive] loadXML: ambient set function id" << fid
                                   << "does not exist (any more) - dropping this entry";
                    else
                        s.functions.append(FunctionRef{ fid, enabled, isGroup, isSuperChaser });
                    root.skipCurrentElement();
                }
                else
                {
                    root.skipCurrentElement();
                }
            }

            m_ambientSets.append(s);
        }
        else if (root.name() == KXMLQLCVCMusicReactiveGroup)
        {
            QXmlStreamAttributes attrs = root.attributes();
            FunctionGroup g;
            g.id = attrs.value(KXMLQLCVCMusicReactiveGroupId).toString().toUInt();
            g.name = attrs.value(KXMLQLCVCMusicReactiveProfileName).toString();
            if (g.name.isEmpty())
                g.name = tr("Group %1").arg(m_groups.count() + 1);
            g.rotate = attrs.hasAttribute(KXMLQLCVCMusicReactiveGroupRotate)
                    && attrs.value(KXMLQLCVCMusicReactiveGroupRotate).toString() == "true";
            g.rotateIntervalSec = attrs.hasAttribute(KXMLQLCVCMusicReactiveGroupInterval)
                    ? attrs.value(KXMLQLCVCMusicReactiveGroupInterval).toString().toInt() : 30;
            if (g.rotateIntervalSec <= 0)
                g.rotateIntervalSec = 30;

            while (root.readNextStartElement())
            {
                if (root.name() == KXMLQLCVCMusicReactiveGroupMember)
                {
                    QXmlStreamAttributes mattrs = root.attributes();
                    quint32 fid = mattrs.value(KXMLQLCVCMusicReactiveFunctionID).toString().toUInt();
                    if (m_doc->function(fid) == nullptr)
                        qWarning() << "[VCMusicReactive] loadXML: group member function id" << fid
                                   << "does not exist (any more) - dropping this entry";
                    else
                        g.members.append(fid);
                    root.skipCurrentElement();
                }
                else
                {
                    root.skipCurrentElement();
                }
            }

            if (g.id == 0)
                g.id = m_nextGroupId; // saved without an id somehow (shouldn't happen) - assign one
            m_nextGroupId = qMax(m_nextGroupId, g.id + 1);
            m_groups.append(g);
        }
        else if (root.name() == KXMLQLCVCMusicReactiveSuperChaser)
        {
            QXmlStreamAttributes attrs = root.attributes();
            SuperChaser sc;
            sc.id = attrs.value(KXMLQLCVCMusicReactiveGroupId).toString().toUInt();
            sc.name = attrs.value(KXMLQLCVCMusicReactiveProfileName).toString();
            if (sc.name.isEmpty())
                sc.name = tr("Super Chaser %1").arg(m_superChasers.count() + 1);

            while (root.readNextStartElement())
            {
                if (root.name() == KXMLQLCVCMusicReactiveSuperChaserTrack)
                {
                    QXmlStreamAttributes tattrs = root.attributes();
                    quint32 fid = tattrs.value(KXMLQLCVCMusicReactiveFunctionID).toString().toUInt();
                    bool enabled = !tattrs.hasAttribute(KXMLQLCVCMusicReactiveEnabled)
                                || tattrs.value(KXMLQLCVCMusicReactiveEnabled).toString() == "true";
                    bool isGroup = tattrs.hasAttribute(KXMLQLCVCMusicReactiveIsGroup)
                                && tattrs.value(KXMLQLCVCMusicReactiveIsGroup).toString() == "true";
                    // A track is never itself another Super Chaser - see
                    // the class comment above superChasers() - so unlike
                    // Profile/Ambient Set entries, no IsSuperChaser check
                    // here. A Group id can't be validated yet (parse
                    // order isn't guaranteed) - handled gracefully at
                    // runtime instead, same as everywhere else.
                    if (!isGroup && m_doc->function(fid) == nullptr)
                        qWarning() << "[VCMusicReactive] loadXML: super chaser track function id" << fid
                                   << "does not exist (any more) - dropping this entry";
                    else
                        sc.tracks.append(FunctionRef{ fid, enabled, isGroup });
                    root.skipCurrentElement();
                }
                else
                {
                    root.skipCurrentElement();
                }
            }

            if (sc.id == 0)
                sc.id = m_nextSuperChaserId;
            m_nextSuperChaserId = qMax(m_nextSuperChaserId, sc.id + 1);
            m_superChasers.append(sc);
        }
        else if (root.name() == KXMLQLCVCMusicReactiveSuperShow)
        {
            QXmlStreamAttributes attrs = root.attributes();
            SuperShow show;
            show.id = attrs.value(KXMLQLCVCMusicReactiveGroupId).toString().toUInt();
            show.name = attrs.value(KXMLQLCVCMusicReactiveProfileName).toString();
            if (show.name.isEmpty())
                show.name = tr("Super Show %1").arg(m_superShows.count() + 1);
            show.loop = attrs.value(KXMLQLCVCMusicReactiveSuperShowLoop).toString() == "true";

            while (root.readNextStartElement())
            {
                if (root.name() == KXMLQLCVCMusicReactiveSuperShowSection)
                {
                    QXmlStreamAttributes sattrs = root.attributes();
                    SuperShowSection sec;
                    sec.profileId = sattrs.value(KXMLQLCVCMusicReactiveProfileID).toString().toUInt();
                    sec.durationSec = qMax(1, sattrs.value(KXMLQLCVCMusicReactiveDurationSec).toString().toInt());
                    // The referenced Profile's own id fixup (see the pass
                    // below) hasn't necessarily run yet at this point in
                    // parse order - not validated here, only at runtime
                    // (profileIndexById() returning -1 just skips that
                    // section, same graceful handling as everywhere else
                    // a saved id might no longer resolve).
                    show.sections.append(sec);
                    root.skipCurrentElement();
                }
                else
                {
                    root.skipCurrentElement();
                }
            }

            if (show.id == 0)
                show.id = m_nextSuperShowId;
            m_nextSuperShowId = qMax(m_nextSuperShowId, show.id + 1);
            m_superShows.append(show);
        }
        else
        {
            qWarning() << Q_FUNC_INFO << "Unknown Music Reactive tag:" << root.name().toString();
            root.skipCurrentElement();
        }
    }

    // Data-only default so a project saved before any Profile was
    // explicitly activated still does something - actually starting it
    // happens once capture is enabled (setCaptureEnabled() calls
    // rotateToNextActiveProfile() if nothing is current yet).
    if (!m_profiles.isEmpty())
    {
        bool anyActive = false;
        for (const MusicProfile &p : m_profiles)
            if (p.active) { anyActive = true; break; }
        if (!anyActive)
            m_profiles[0].active = true;
    }

    // Same fallback, for the same reason, for Ambient Sets - actually
    // selecting one as current happens the next time a break starts
    // (applyBreakTransition() -> rotateToNextActiveAmbientSet()), not
    // here.
    if (!m_ambientSets.isEmpty())
    {
        bool anyActive = false;
        for (const AmbientSet &s : m_ambientSets)
            if (s.active) { anyActive = true; break; }
        if (!anyActive)
            m_ambientSets[0].active = true;
    }

    // Assign a stable id to any Profile/Ambient Set that doesn't already
    // have one - either it predates this attribute entirely, or (the
    // legacy-migration branches above) never set one in the first
    // place. Done as a single pass at the end, after every Profile/Set
    // has been parsed, so m_nextProfileId/m_nextAmbientSetId end up
    // correctly past every id actually in use, real or freshly assigned.
    for (MusicProfile &p : m_profiles)
    {
        if (p.id == 0)
            p.id = m_nextProfileId;
        m_nextProfileId = qMax(m_nextProfileId, p.id + 1);
    }
    for (AmbientSet &s : m_ambientSets)
    {
        if (s.id == 0)
            s.id = m_nextAmbientSetId;
        m_nextAmbientSetId = qMax(m_nextAmbientSetId, s.id + 1);
    }

    syncMidiControls();

    qDebug() << "[VCMusicReactive] loadXML done: profiles" << m_profiles.count()
             << "ambientSets" << m_ambientSets.count() << "groups" << m_groups.count()
             << "superChasers" << m_superChasers.count() << "superShows" << m_superShows.count();

    return true;
}

bool VCMusicReactive::saveXML(QXmlStreamWriter *doc) const
{
    Q_ASSERT(doc != nullptr);

    doc->writeStartElement(KXMLQLCVCMusicReactive);

    saveXMLCommon(doc);
    saveXMLWindowState(doc);
    saveXMLAppearance(doc);

    QStringList intensityParts;
    for (int i = 0; i < BandCount; i++)
        intensityParts << QString::number(m_intensities[i]);
    doc->writeTextElement(KXMLQLCVCMusicReactiveIntensities, intensityParts.join(","));

    QStringList thresholdParts;
    for (int i = 0; i < BandCount; i++)
        thresholdParts << QString::number(m_thresholds[i]);
    doc->writeTextElement(KXMLQLCVCMusicReactiveThresholds, thresholdParts.join(","));

    doc->writeTextElement(KXMLQLCVCMusicReactiveAutoMode, m_autoMode ? "true" : "false");
    doc->writeTextElement(KXMLQLCVCMusicReactiveAdvanceOnBeat, m_advanceOnBeat ? "true" : "false");

    for (const MusicProfile &p : m_profiles)
    {
        doc->writeStartElement(KXMLQLCVCMusicReactiveProfile);
        doc->writeAttribute(KXMLQLCVCMusicReactiveGroupId, QString::number(p.id));
        doc->writeAttribute(KXMLQLCVCMusicReactiveProfileName, p.name);
        doc->writeAttribute(KXMLQLCVCMusicReactiveActive, p.active ? "true" : "false");
        for (const FunctionRef &ref : p.chasers)
        {
            doc->writeStartElement(KXMLQLCVCMusicReactiveProfileChaser);
            doc->writeAttribute(KXMLQLCVCMusicReactiveFunctionID, QString::number(ref.id));
            doc->writeAttribute(KXMLQLCVCMusicReactiveEnabled, ref.enabled ? "true" : "false");
            if (ref.isGroup)
                doc->writeAttribute(KXMLQLCVCMusicReactiveIsGroup, "true");
            if (ref.isSuperChaser)
                doc->writeAttribute(KXMLQLCVCMusicReactiveIsSuperChaser, "true");
            doc->writeEndElement();
        }
        doc->writeEndElement();
    }

    for (const AmbientSet &s : m_ambientSets)
    {
        doc->writeStartElement(KXMLQLCVCMusicReactiveAmbientSet);
        doc->writeAttribute(KXMLQLCVCMusicReactiveGroupId, QString::number(s.id));
        doc->writeAttribute(KXMLQLCVCMusicReactiveProfileName, s.name);
        doc->writeAttribute(KXMLQLCVCMusicReactiveActive, s.active ? "true" : "false");
        for (const FunctionRef &ref : s.functions)
        {
            doc->writeStartElement(KXMLQLCVCMusicReactiveAmbientSetFunction);
            doc->writeAttribute(KXMLQLCVCMusicReactiveFunctionID, QString::number(ref.id));
            doc->writeAttribute(KXMLQLCVCMusicReactiveEnabled, ref.enabled ? "true" : "false");
            if (ref.isGroup)
                doc->writeAttribute(KXMLQLCVCMusicReactiveIsGroup, "true");
            if (ref.isSuperChaser)
                doc->writeAttribute(KXMLQLCVCMusicReactiveIsSuperChaser, "true");
            doc->writeEndElement();
        }
        doc->writeEndElement();
    }

    for (const FunctionGroup &g : m_groups)
    {
        doc->writeStartElement(KXMLQLCVCMusicReactiveGroup);
        doc->writeAttribute(KXMLQLCVCMusicReactiveGroupId, QString::number(g.id));
        doc->writeAttribute(KXMLQLCVCMusicReactiveProfileName, g.name);
        doc->writeAttribute(KXMLQLCVCMusicReactiveGroupRotate, g.rotate ? "true" : "false");
        doc->writeAttribute(KXMLQLCVCMusicReactiveGroupInterval, QString::number(g.rotateIntervalSec));
        for (quint32 fid : g.members)
        {
            doc->writeStartElement(KXMLQLCVCMusicReactiveGroupMember);
            doc->writeAttribute(KXMLQLCVCMusicReactiveFunctionID, QString::number(fid));
            doc->writeEndElement();
        }
        doc->writeEndElement();
    }

    for (const SuperChaser &sc : m_superChasers)
    {
        doc->writeStartElement(KXMLQLCVCMusicReactiveSuperChaser);
        doc->writeAttribute(KXMLQLCVCMusicReactiveGroupId, QString::number(sc.id));
        doc->writeAttribute(KXMLQLCVCMusicReactiveProfileName, sc.name);
        for (const FunctionRef &ref : sc.tracks)
        {
            doc->writeStartElement(KXMLQLCVCMusicReactiveSuperChaserTrack);
            doc->writeAttribute(KXMLQLCVCMusicReactiveFunctionID, QString::number(ref.id));
            doc->writeAttribute(KXMLQLCVCMusicReactiveEnabled, ref.enabled ? "true" : "false");
            if (ref.isGroup)
                doc->writeAttribute(KXMLQLCVCMusicReactiveIsGroup, "true");
            doc->writeEndElement();
        }
        doc->writeEndElement();
    }

    for (const SuperShow &show : m_superShows)
    {
        doc->writeStartElement(KXMLQLCVCMusicReactiveSuperShow);
        doc->writeAttribute(KXMLQLCVCMusicReactiveGroupId, QString::number(show.id));
        doc->writeAttribute(KXMLQLCVCMusicReactiveProfileName, show.name);
        doc->writeAttribute(KXMLQLCVCMusicReactiveSuperShowLoop, show.loop ? "true" : "false");
        for (const SuperShowSection &sec : show.sections)
        {
            doc->writeStartElement(KXMLQLCVCMusicReactiveSuperShowSection);
            doc->writeAttribute(KXMLQLCVCMusicReactiveProfileID, QString::number(sec.profileId));
            doc->writeAttribute(KXMLQLCVCMusicReactiveDurationSec, QString::number(sec.durationSec));
            doc->writeEndElement();
        }
        doc->writeEndElement();
    }

    doc->writeEndElement();

    return true;
}
