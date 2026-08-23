/*
  Q Light Controller Plus
  vcmusicreactive.h

  A purpose-built music-reactive widget: live Master/Kick/Mids level
  meters (fed from AudioCapture::analysisStateChanged - see
  engine/audio/src/audioevents.h for where those values come from),
  a per-band intensity/sensitivity fader, a switchable list of Chasers
  advanced one step per detected Kick (AudioCapture::audioEventDetected,
  AudioEventType::Kick), and a list of ambient Scenes/Chasers that take
  over once the track stops actually landing kicks on the beat grid -
  e.g. a breakdown - handing back control once it does again.
  Deliberately a RHYTHMIC PATTERN check, not a volume one: a breakdown
  can still be loud (pads, vocals, a sub riser) and a good mix rarely
  goes near-silent, so "is there a kick roughly every beat" is the
  actual signal, same as a human glancing at a waveform overview would
  read it - see the comment above kOnGridToleranceFrac in
  slotAudioEventDetected() for how that's done, and what was tried and
  rejected first (plain bassEnergy - looked reasonable on a hard-cut
  breakdown but was consistently several seconds late on the far more
  common gradual fade-out; the engine's own saturated per-band onset
  signal - relative to its own recent peak by design, so it carries no
  absolute-loudness information at all; total energy across all bands -
  vocals/pads keep that elevated through a break with no kick). Ground-
  truthed against real recorded clips via engine/test/wavanalyze at each
  step, not just live guesswork.

  This intentionally does NOT reuse VCAudioTriggers/AudioBar's
  amplitude-threshold-bar model: that model is a poor fit for "pick one
  of several chasers, advance it on a real detected Kick, and fall back
  to ambient during a break" - this widget is a dedicated, from-scratch
  design for exactly that job. VCAudioTriggers/AudioBar are untouched
  and keep working exactly as before for their original purpose
  (frequency-band amplitude thresholds driving DMX/Function/Widget
  bars).

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

#ifndef VCMUSICREACTIVE_H
#define VCMUSICREACTIVE_H

#include <QElapsedTimer>

#include "vcwidget.h"

#define KXMLQLCVCMusicReactive QStringLiteral("MusicReactive")

class AudioCapture;
class VirtualConsole;
class Chaser;

class VCMusicReactive : public VCWidget
{
    Q_OBJECT

    Q_PROPERTY(bool captureEnabled READ captureEnabled WRITE setCaptureEnabled NOTIFY captureEnabledChanged)
    Q_PROPERTY(QVariantList levels READ levels NOTIFY levelsChanged)
    Q_PROPERTY(QVariantList intensities READ intensities NOTIFY intensitiesChanged)
    Q_PROPERTY(QVariantList thresholds READ thresholds NOTIFY thresholdsChanged)
    Q_PROPERTY(bool inBreak READ inBreak NOTIFY inBreakChanged)
    Q_PROPERTY(bool autoMode READ autoMode WRITE setAutoMode NOTIFY autoModeChanged)
    Q_PROPERTY(bool advanceOnBeat READ advanceOnBeat WRITE setAdvanceOnBeat NOTIFY advanceOnBeatChanged)
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY profilesChanged)
    Q_PROPERTY(QVariantList visibleChasers READ visibleChasers NOTIFY visibleChasersChanged)
    Q_PROPERTY(int currentProfileIndex READ currentProfileIndex NOTIFY visibleChasersChanged)
    Q_PROPERTY(QVariantList ambientFunctions READ ambientFunctions NOTIFY ambientFunctionsChanged)
    Q_PROPERTY(QString debugText READ debugText NOTIFY debugTextChanged)

    /*********************************************************************
     * Initialization
     *********************************************************************/
public:
    VCMusicReactive(Doc *doc = nullptr, VirtualConsole *vc = nullptr, QObject *parent = nullptr);
    ~VCMusicReactive();

    /** @reimp */
    QString defaultCaption() const override;

    /** @reimp */
    void setupLookAndFeel(qreal pixelDensity, int page) override;

    /** @reimp */
    void render(QQuickView *view, QQuickItem *parent) override;

    /** @reimp */
    QString propertiesResource() const override;

    /** @reimp */
    VCWidget *createCopy(VCWidget *parent) const override;

    /** @reimp */
    void remapChannels(const QMap<SceneValue, SceneValue> &remapMap) override;

protected:
    /** @reimp */
    bool copyFrom(const VCWidget *widget) override;

private:
    FunctionParent functionParent() const;
    void applyBreakTransition(bool newBreak);

    /*********************************************************************
     * Capture
     *********************************************************************/
public:
    bool captureEnabled() const;
    void setCaptureEnabled(bool enable);

signals:
    void captureEnabledChanged();

    /*********************************************************************
     * Levels / intensity bands
     *********************************************************************/
public:
    enum Band { Master = 0, Kick = 1, Mids = 2, BandCount = 3 };
    Q_ENUM(Band)

    /** Live 0..255 meter values: [master energy, kick/bass energy, mids energy] */
    QVariantList levels() const;

    /** User-settable 0..100 per-band gain, persisted. Purely scales how
     *  tall each meter reads (a quiet input source needs more gain to
     *  usefully fill the meter) - it does NOT affect triggering; that's
     *  what thresholds (below) are for. */
    QVariantList intensities() const;
    Q_INVOKABLE void setIntensity(int band, int percent);

    /** User-settable 0..100 per-band marker, persisted and drawn as a
     *  draggable line on each meter. Purely visual for now - the chaser
     *  advances once per detected Kick (see slotAudioEventDetected()),
     *  not off any of these markers - kept for visual calibration and
     *  as a ready-made control for whenever a band drives its own
     *  action again. */
    QVariantList thresholds() const;
    Q_INVOKABLE void setThreshold(int band, int percent);

    bool inBreak() const;

    /** true (default): fully automatic - Ambient/Active is decided by
     *  whether Kicks are still landing on the beat grid, same as always.
     *  false: a manual
     *  safety net for live use (e.g. a presentation/show where the room
     *  audio can't be trusted to behave) - the automatic break decision
     *  is suspended and Ambient/Active is instead whatever
     *  setManualBreak() last set, so the operator can force the right
     *  state by hand from the touch screen regardless of what the
     *  analysis thinks. The Kick-driven chaser advance and the meters
     *  keep working the same in both modes - only the break/ambient
     *  decision itself is what gets taken over. */
    bool autoMode() const;
    void setAutoMode(bool enable);

    /** Only takes effect while autoMode is false - see autoMode(). */
    Q_INVOKABLE void setManualBreak(bool enable);

    /** One-line, always-current diagnostic readout (raw engine energy,
     *  gained meter level, threshold, break state) so the user can see
     *  exactly why a Kick is or isn't advancing the chaser without
     *  needing to run with --debug and read the terminal. Temporary/
     *  cheap to remove once the trigger behaviour is dialed in, but
     *  genuinely useful for calibrating Gain/threshold against a real
     *  input level. */
    QString debugText() const;

signals:
    void levelsChanged();
    void intensitiesChanged();
    void thresholdsChanged();
    void inBreakChanged();
    void autoModeChanged();
    void debugTextChanged();

protected slots:
    void slotAnalysisStateChanged(double bpm, double beatPhase, int beatIndex, int barIndex,
                                  double energy, double bassEnergy, double midEnergy, double highEnergy,
                                  double kickConfidence, double beatConfidence, bool inBreak,
                                  double peakBandValue, double peakThreshold, double kickBandOnset);
    void slotAudioEventDetected(int type, double timestampSec, double confidence, double strength);

    /*********************************************************************
     * Profiles: named groups of Chasers. Any number of Profiles can be
     * marked active at once; exactly one of the active Profiles is
     * "current" at any moment and its enabled Chasers are the ones
     * actually running (together, simultaneously - e.g. a PAR chaser
     * and a moving-head chaser from the same Profile stepping in
     * lockstep) and advanced by every Kick/Beat (see advanceOnBeat).
     * With more than one Profile active, which one is current rotates
     * automatically every kRotateEveryBars bars (see
     * slotAnalysisStateChanged()) - that's what gives the "keeps
     * bringing different effects back around" behaviour, distinct from
     * "run everything at once forever". A single active Profile just
     * stays current forever - no visible rotation.
     *********************************************************************/
public:
    /** [{ name, active }, ...] - the Profile list itself, for the
     *  left-hand panel in the live widget (VCMusicReactiveItem.qml). */
    QVariantList profiles() const;

    /** [{ id, name, enabled, running, profileIndex, profileName }, ...]
     *  - every Chaser belonging to any ACTIVE Profile, flattened across
     *  Profiles, for the right-hand panel: pick individual Chasers on
     *  or off live regardless of which Profile is currently in control
     *  (e.g. "moving heads off" without deactivating the whole
     *  Profile). running = true only for entries whose Profile is the
     *  current one AND that are themselves enabled - i.e. actually
     *  outputting DMX right now, the green/gray border in the UI. */
    QVariantList visibleChasers() const;

    /** -1 if no Profile is currently in control. */
    int currentProfileIndex() const;

    Q_INVOKABLE void addProfile(const QString &name);
    Q_INVOKABLE void removeProfileAt(int index);
    Q_INVOKABLE void renameProfile(int index, const QString &name);
    Q_INVOKABLE void setProfileActive(int index, bool active);

    Q_INVOKABLE void addChaserToProfile(int profileIndex, quint32 functionId);
    Q_INVOKABLE void removeChaserFromProfile(int profileIndex, int chaserIndex);
    Q_INVOKABLE void setChaserEnabledInProfile(int profileIndex, int chaserIndex, bool enabled);

    /** false (default): Chasers advance one step per detected Kick -
     *  precise (steps land on real hits) but the classifier is sparse
     *  by nature, so on a track where it doesn't fire often they can
     *  feel like they barely move. true: advance one step per tracked
     *  Beat instead - the tempo tracker locks on far more reliably/
     *  often than any single onset gets classified, so this reads as a
     *  smooth, mechanical, always-in-tempo step regardless of how the
     *  Kick classifier is doing on this particular track. Independent
     *  of autoMode - applies the same in both Auto and Manual. */
    bool advanceOnBeat() const;
    void setAdvanceOnBeat(bool enable);

signals:
    void profilesChanged();
    void visibleChasersChanged();
    void advanceOnBeatChanged();

private:
    void startOrStepChaser(quint32 functionId, double masterIntensity);
    void advanceCurrentProfileChasers(double confidence);
    void stopCurrentProfileChasers();
    void startCurrentProfileChasers();
    void rotateToNextActiveProfile();
    void tryAdvanceChaser(double confidence, const char *reason);

    /*********************************************************************
     * Ambient functions (run during a break, any number enabled at once)
     *********************************************************************/
public:
    /** [{ id, name, enabled }, ...] */
    QVariantList ambientFunctions() const;

    Q_INVOKABLE void addAmbientFunction(quint32 functionId);
    Q_INVOKABLE void removeAmbientAt(int index);
    Q_INVOKABLE void setAmbientEnabled(int index, bool enabled);

signals:
    void ambientFunctionsChanged();

private:
    void startEnabledAmbientFunctions();
    void stopAllAmbientFunctions();

    /*********************************************************************
     * Load & Save
     *********************************************************************/
public:
    bool loadXML(QXmlStreamReader &root) override;
    bool saveXML(QXmlStreamWriter *doc) const override;

    /*********************************************************************
     * Members
     *********************************************************************/
private:
    VirtualConsole *m_vc;
    AudioCapture *m_inputCapture;
    bool m_captureEnabled;

    QVariantList m_levels;         // live, not persisted
    int m_intensities[BandCount];  // 0..100, persisted (gain)
    int m_thresholds[BandCount];   // 0..100, persisted (trigger marker)
    QString m_debugText;
    // Per-chaser start/step outcome from the last advanceCurrentProfileChasers()
    // call - temporary diagnostic for tracking down multi-Chaser-per-Profile
    // issues, shown in debugText.
    QString m_lastChaserStatus;
    // Unconditional count of every Kick AudioEvent received from the
    // engine, regardless of gate/break state - a ground-truth check for
    // whether the onset detector is firing on real captured audio at
    // all, independent of anything this widget does with the result.
    // Temporary, same lifetime as m_debugText.
    int m_kickEventCount = 0;
    int m_lastKickStrengthLevel = -1; // last Kick event's strength, 0..255 scale, -1 = none yet
    int m_beatEventCount = 0;         // Beat events received (diagnostic only, doesn't drive anything)
    int m_clapEventCount = 0;         // Snare/clap AudioEvents (diagnostic only for now)

    // Call-counting trail for tryAdvanceChaser()/startOrStepChaser() -
    // added because two rounds of live testing plus a standalone
    // engine-level reproduction (engine/test/chaserprofilesim) showed
    // the step-advance mechanism itself works correctly when driven
    // this exact way, which means the live failure has to be somewhere
    // between "a Beat/Kick event arrives" and "startOrStepChaser()
    // actually gets called" - these counters make that chain visible
    // from a single debugText snapshot instead of needing to scroll
    // back through qDebug output. See debugText()'s "adv:" field.
    int m_tryAdvanceCalls = 0;        // tryAdvanceChaser() entered, any reason
    int m_tryAdvanceBlockedBreak = 0; // ...and bailed: m_inBreak
    int m_tryAdvanceRateLimited = 0;  // ...and bailed: kMinAdvanceGapMs
    int m_tryAdvanceAccepted = 0;     // ...and actually called advanceCurrentProfileChasers()
    int m_startOrStepCalls = 0;       // startOrStepChaser() entered, any chaser
    int m_startOrStepFreshStarts = 0; // ...took the "not running" (fresh start) branch
    int m_startOrStepAdvances = 0;    // ...took the "already running" (ChaserNextStep) branch

    // Continuous beat-grid reading, cached from the last
    // analysisStateChanged block so a Kick event (a separate,
    // asynchronous signal) can check "was this near a predicted beat"
    // without needing its own copy of the tempo tracker - see
    // slotAudioEventDetected()'s Kick handling.
    double m_lastBeatPhase = 0.0;  // 0..1 within the current beat
    double m_lastBpm = 0.0;        // 0 = tempo not locked yet
    double m_lastBassEnergy = 0.0; // sampled at each Beat - see below

    // Debug-readout only now: how long since a Kick last landed
    // on-grid, reset on every on-grid Kick - see
    // slotAudioEventDetected()'s Kick handling. Not what break actually
    // uses (see m_recentBeatHits) - individually classified Kicks are
    // too sparse on their own (confirmed against 4 real recordings via
    // engine/test/wavanalyze: gating break directly on this flickered
    // Active/Ambient every ~1.8s mid-song, regardless of hold value).
    QElapsedTimer m_lastOnGridKickTimer;
    bool m_lastOnGridKickTimerValid = false;

    // Break/ambient decision: a rolling window of the last N tracked
    // Beats, each marked "hit" if bassEnergy read above a floor AT that
    // beat - i.e. literally "was there a kick roughly on this quarter
    // note", sampled at the Beat tracker's own reliable, tempo-locked
    // timing rather than depending on the sparse Kick classifier for
    // the timing too. Break = fewer than half of the window are hits.
    // Beats (not milliseconds) as the window unit is deliberate: it's
    // musically coherent (scales with tempo on its own) and doubles as
    // the debounce - no separate hold timer needed once several beats'
    // worth of history has to actually agree.
    QVector<bool> m_recentBeatHits;

    // Safety net against rapid flapping: a borderline/noisy pattern
    // read could in principle cross the hold threshold back and forth
    // faster than intended, and repeatedly restarting Scenes/Chasers
    // before their fade completes can visibly "runaway" as overlapping
    // fade-ins stack. Automatic transitions INTO break only (never
    // manual ones, and never the return to Active - see
    // slotAnalysisStateChanged()) are rate-limited to at most one every
    // kMinEnterBreakGapMs.
    QElapsedTimer m_lastAutoTransitionTimer;
    bool m_lastAutoTransitionTimerValid = false;

    // Same idea, for Kick-triggered chaser advances - see
    // slotAudioEventDetected()'s Kick handling.
    QElapsedTimer m_lastAdvanceTimer;
    bool m_lastAdvanceTimerValid = false;

    bool m_inBreak;
    bool m_autoMode = true; // persisted - see autoMode()
    bool m_advanceOnBeat = false; // persisted - see advanceOnBeat()

    struct FunctionRef
    {
        quint32 id;
        bool enabled; // only meaningful for ambient entries and profile chasers
    };
    QVector<FunctionRef> m_ambient;

    struct MusicProfile
    {
        QString name;
        QVector<FunctionRef> chasers;
        bool active = false; // selected - participates in rotation
    };
    QVector<MusicProfile> m_profiles;
    // Index into m_profiles of whichever active Profile is currently
    // driving output, or -1 if none are active. See the class comment
    // above profiles() for the rotation model.
    int m_currentProfileIndex = -1;
    int m_lastRotateBarIndex = -1; // barIndex last time we rotated, -1 = never yet
};

#endif // VCMUSICREACTIVE_H
