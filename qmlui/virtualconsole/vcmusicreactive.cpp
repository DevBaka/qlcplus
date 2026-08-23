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
#include "audiocapture.h"
#include "audioevents.h"
#include "chaseraction.h"
#include "mastertimer.h"
#include "chaser.h"
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
    m_ambient = mr->m_ambient;
    m_autoMode = mr->m_autoMode;
    m_advanceOnBeat = mr->m_advanceOnBeat;

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

        stopCurrentProfileChasers();
        stopAllAmbientFunctions();
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
        startEnabledAmbientFunctions();
    }
    else
    {
        stopAllAmbientFunctions();
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
        // there (stopActiveChaser()/stopAllAmbientFunctions() are the
        // safe direction). An earlier version rate-limited both
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
        map.insert("name", p.name);
        map.insert("active", p.active);

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
            Function *f = m_doc->function(ref.id);
            cmap.insert("name", f ? f->name() : tr("(missing)"));
            cmap.insert("enabled", ref.enabled);
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
            Function *f = m_doc->function(ref.id);
            map.insert("name", f ? f->name() : tr("(missing)"));
            map.insert("enabled", ref.enabled);
            map.insert("running", ref.enabled && pi == m_currentProfileIndex);
            map.insert("profileIndex", pi);
            map.insert("profileName", p.name);
            map.insert("chaserIndex", ci);
            list << map;
        }
    }
    return list;
}

int VCMusicReactive::currentProfileIndex() const
{
    return m_currentProfileIndex;
}

void VCMusicReactive::addProfile(const QString &name)
{
    MusicProfile p;
    p.name = name.isEmpty() ? tr("Profile %1").arg(m_profiles.count() + 1) : name;
    m_profiles.append(p);
    emit profilesChanged();
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
}

void VCMusicReactive::renameProfile(int index, const QString &name)
{
    if (index < 0 || index >= m_profiles.count() || name.isEmpty())
        return;

    m_profiles[index].name = name;
    emit profilesChanged();
    emit visibleChasersChanged();
}

void VCMusicReactive::setProfileActive(int index, bool active)
{
    if (index < 0 || index >= m_profiles.count() || m_profiles[index].active == active)
        return;

    m_profiles[index].active = active;
    emit profilesChanged();

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
        if (ref.id == functionId)
            return; // already present

    chasers.append(FunctionRef{ functionId, true });
    emit profilesChanged();
    emit visibleChasersChanged();
}

void VCMusicReactive::removeChaserFromProfile(int profileIndex, int chaserIndex)
{
    if (profileIndex < 0 || profileIndex >= m_profiles.count())
        return;
    QVector<FunctionRef> &chasers = m_profiles[profileIndex].chasers;
    if (chaserIndex < 0 || chaserIndex >= chasers.count())
        return;

    if (profileIndex == m_currentProfileIndex)
    {
        Chaser *ch = qobject_cast<Chaser*>(m_doc->function(chasers[chaserIndex].id));
        if (ch != nullptr && ch->isRunning())
            ch->stop(functionParent());
    }

    chasers.remove(chaserIndex);
    emit profilesChanged();
    emit visibleChasersChanged();
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

    // Live effect, right now, only if this Profile is the one actually
    // in control and not currently in a break (chasers are meant to be
    // stopped then regardless) - otherwise the flag just takes effect
    // whenever this Profile next becomes current/active resumes.
    if (profileIndex == m_currentProfileIndex && !m_inBreak)
    {
        if (enabled)
        {
            double masterIntensity = qBound(0.0, double(m_intensities[Master]) / 100.0, 1.0);
            startOrStepChaser(chasers[chaserIndex].id, masterIntensity);
        }
        else
        {
            Chaser *ch = qobject_cast<Chaser*>(m_doc->function(chasers[chaserIndex].id));
            if (ch != nullptr && ch->isRunning())
                ch->stop(functionParent());
        }
    }

    emit visibleChasersChanged();
}

void VCMusicReactive::startOrStepChaser(quint32 functionId, double masterIntensity)
{
    m_startOrStepCalls++;

    Function *func = m_doc->function(functionId);
    Chaser *ch = qobject_cast<Chaser*>(func);
    if (ch == nullptr)
    {
        qDebug() << "[VCMusicReactive] startOrStepChaser: function id" << functionId
                 << "is not a Chaser (func ptr" << func << ")";
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
            startOrStepChaser(ref.id, masterIntensity);
        else
            m_lastChaserStatus += QString("%1[disabled] ").arg(m_doc->function(ref.id) ? m_doc->function(ref.id)->name() : "?");
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
    {
        Chaser *ch = qobject_cast<Chaser*>(m_doc->function(ref.id));
        if (ch != nullptr && ch->isRunning())
            ch->stop(functionParent());
    }
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
 * Ambient functions
 *********************************************************************/

QVariantList VCMusicReactive::ambientFunctions() const
{
    QVariantList list;
    for (const FunctionRef &ref : m_ambient)
    {
        QVariantMap map;
        map.insert("id", ref.id);
        map.insert("enabled", ref.enabled);
        Function *f = m_doc->function(ref.id);
        map.insert("name", f ? f->name() : tr("(missing)"));
        list << map;
    }
    return list;
}

void VCMusicReactive::addAmbientFunction(quint32 functionId)
{
    if (m_doc->function(functionId) == nullptr)
        return;

    for (const FunctionRef &ref : m_ambient)
        if (ref.id == functionId)
            return;

    m_ambient.append(FunctionRef{ functionId, true });
    emit ambientFunctionsChanged();

    if (m_inBreak)
        startEnabledAmbientFunctions();
}

void VCMusicReactive::removeAmbientAt(int index)
{
    if (index < 0 || index >= m_ambient.count())
        return;

    Function *f = m_doc->function(m_ambient[index].id);
    if (f != nullptr && f->isRunning())
        f->stop(functionParent());

    m_ambient.remove(index);
    emit ambientFunctionsChanged();
}

void VCMusicReactive::setAmbientEnabled(int index, bool enabled)
{
    if (index < 0 || index >= m_ambient.count())
        return;

    m_ambient[index].enabled = enabled;
    emit ambientFunctionsChanged();

    if (!m_inBreak)
        return;

    Function *f = m_doc->function(m_ambient[index].id);
    if (f == nullptr)
        return;

    if (enabled && !f->isRunning())
        f->start(m_doc->masterTimer(), functionParent());
    else if (!enabled && f->isRunning())
        f->stop(functionParent());
}

void VCMusicReactive::startEnabledAmbientFunctions()
{
    for (const FunctionRef &ref : m_ambient)
    {
        if (!ref.enabled)
            continue;
        Function *f = m_doc->function(ref.id);
        if (f != nullptr && !f->isRunning())
            f->start(m_doc->masterTimer(), functionParent());
    }
}

void VCMusicReactive::stopAllAmbientFunctions()
{
    for (const FunctionRef &ref : m_ambient)
    {
        Function *f = m_doc->function(ref.id);
        if (f != nullptr && f->isRunning())
            f->stop(functionParent());
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
                    if (m_doc->function(fid) == nullptr)
                        qWarning() << "[VCMusicReactive] loadXML: profile chaser function id" << fid
                                   << "does not exist (any more) - dropping this entry";
                    else
                        p.chasers.append(FunctionRef{ fid, enabled });
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
                m_profiles[legacyIndex].chasers.append(FunctionRef{ fid, true });
            }
            root.skipCurrentElement();
        }
        else if (root.name() == KXMLQLCVCMusicReactiveAmbient)
        {
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
                m_ambient.append(FunctionRef{ fid, enabled });
            }
            root.skipCurrentElement();
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

    qDebug() << "[VCMusicReactive] loadXML done: profiles" << m_profiles.count()
             << "ambient" << m_ambient.count();

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
        doc->writeAttribute(KXMLQLCVCMusicReactiveProfileName, p.name);
        doc->writeAttribute(KXMLQLCVCMusicReactiveActive, p.active ? "true" : "false");
        for (const FunctionRef &ref : p.chasers)
        {
            doc->writeStartElement(KXMLQLCVCMusicReactiveProfileChaser);
            doc->writeAttribute(KXMLQLCVCMusicReactiveFunctionID, QString::number(ref.id));
            doc->writeAttribute(KXMLQLCVCMusicReactiveEnabled, ref.enabled ? "true" : "false");
            doc->writeEndElement();
        }
        doc->writeEndElement();
    }

    for (const FunctionRef &ref : m_ambient)
    {
        doc->writeStartElement(KXMLQLCVCMusicReactiveAmbient);
        doc->writeAttribute(KXMLQLCVCMusicReactiveFunctionID, QString::number(ref.id));
        doc->writeAttribute(KXMLQLCVCMusicReactiveEnabled, ref.enabled ? "true" : "false");
        doc->writeEndElement();
    }

    doc->writeEndElement();

    return true;
}
