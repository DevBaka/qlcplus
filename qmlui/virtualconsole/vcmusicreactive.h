/*
  Q Light Controller Plus
  vcmusicreactive.h

  A purpose-built music-reactive widget: live Master/Kick/Mids level
  meters (fed from AudioCapture::analysisStateChanged - see
  engine/audio/src/audioevents.h for where those values come from),
  a per-band intensity/sensitivity fader, Profiles of Chasers advanced
  one step per detected Kick (AudioCapture::audioEventDetected,
  AudioEventType::Kick), and Ambient Sets of Scenes/Chasers that take
  over once the track stops actually landing kicks on the beat grid -
  e.g. a breakdown - handing back control once it does again. Both
  Profiles and Ambient Sets support the same "more than one active"
  rotation model (see the class comments above profiles()/ambientSets())
  so neither the active nor the idle state has to look the same every
  single time it comes back around.
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
#include <QHash>

#include "vcwidget.h"

#define KXMLQLCVCMusicReactive QStringLiteral("MusicReactive")

class AudioCapture;
class VirtualConsole;
class Chaser;

class VCMusicReactive : public VCWidget
{
    Q_OBJECT

    // Forward declaration only - fully defined down in the Members
    // section below. Needed this early because several private helper
    // method signatures further down (advanceProfileEntry() and friends)
    // take a "const FunctionRef &" parameter.
    struct FunctionRef;

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
    Q_PROPERTY(QVariantList visibleAmbientFunctions READ visibleAmbientFunctions NOTIFY ambientFunctionsChanged)
    Q_PROPERTY(QVariantList ambientSets READ ambientSets NOTIFY ambientSetsChanged)
    Q_PROPERTY(int currentAmbientSetIndex READ currentAmbientSetIndex NOTIFY ambientSetsChanged)
    Q_PROPERTY(QVariantList groups READ groups NOTIFY groupsChanged)
    Q_PROPERTY(QVariantList superChasers READ superChasers NOTIFY superChasersChanged)
    Q_PROPERTY(QVariantList superShows READ superShows NOTIFY superShowsChanged)
    Q_PROPERTY(int activeSuperShowIndex READ activeSuperShowIndex NOTIFY superShowPlaybackChanged)
    Q_PROPERTY(int activeSuperShowSectionIndex READ activeSuperShowSectionIndex NOTIFY superShowPlaybackChanged)
    Q_PROPERTY(qint64 superShowElapsedMs READ superShowElapsedMs NOTIFY superShowPlaybackChanged)
    Q_PROPERTY(bool midiEditMode READ midiEditMode WRITE setMidiEditMode NOTIFY midiEditModeChanged)
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
    /** Adds a Group (see the class comment above groups()) as an entry
     *  of this Profile, right alongside plain Chasers - groupId is
     *  FunctionGroup::id, NOT an index into groups(). */
    Q_INVOKABLE void addGroupToProfile(int profileIndex, quint32 groupId);
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
    /** Index into m_profiles for a given MusicProfile::id, or -1 - same
     *  role as groupIndexById()/superChaserIndexById(), needed by
     *  Super Show playback (see tickSuperShow()) to turn a section's
     *  saved profileId into an index. */
    int profileIndexById(quint32 profileId) const;

    /*********************************************************************
     * Ambient sets: same idea as Profiles (see above), but for the idle/
     * break state. Each Ambient Set is a named group of Scenes/Chasers
     * that run together while idle. Any number of Sets can be marked
     * active; exactly one is "current" and its enabled members are what
     * actually runs. Unlike a Profile (which rotates on a bar count
     * while it's in control), an Ambient Set rotates once per BREAK
     * ENTERED - every time the track goes quiet again it picks the next
     * active Set round-robin, so a long set doesn't play the exact same
     * idle combo every single time it dips - that's the "etwas mehr
     * Abwechslung" this was built for. A single active Set just stays
     * current forever, same as a single active Profile.
     *********************************************************************/
public:
    /** [{ name, active, functions }, ...] - the Ambient Set list, edited
     *  in the widget's Properties panel (add/rename/delete Sets, add/
     *  remove/enable functions within one) - same shape/role as
     *  profiles(). */
    QVariantList ambientSets() const;

    /** [{ id, name, enabled, running, setIndex, setName, functionIndex },
     *  ...] - every function belonging to any ACTIVE Ambient Set,
     *  flattened across Sets, for the live widget's "Idle" panel: pick
     *  individual Scenes/Chasers on or off live regardless of which Set
     *  is currently in control - same shape/role as visibleChasers().
     *  running = true only for entries whose Set is the current one AND
     *  that are themselves enabled. */
    QVariantList visibleAmbientFunctions() const;

    /** -1 if no Ambient Set is currently selected to run (falls back to
     *  nothing playing during a break, same as no active Profile falls
     *  back to nothing playing while active). */
    int currentAmbientSetIndex() const;

    Q_INVOKABLE void addAmbientSet(const QString &name);
    Q_INVOKABLE void removeAmbientSetAt(int index);
    Q_INVOKABLE void renameAmbientSet(int index, const QString &name);
    Q_INVOKABLE void setAmbientSetActive(int index, bool active);

    Q_INVOKABLE void addFunctionToAmbientSet(int setIndex, quint32 functionId);
    /** Same as addGroupToProfile(), for an Ambient Set instead. */
    Q_INVOKABLE void addGroupToAmbientSet(int setIndex, quint32 groupId);
    Q_INVOKABLE void removeFunctionFromAmbientSet(int setIndex, int functionIndex);
    Q_INVOKABLE void setFunctionEnabledInAmbientSet(int setIndex, int functionIndex, bool enabled);

signals:
    void ambientFunctionsChanged();
    void ambientSetsChanged();

private:
    void startCurrentAmbientSetFunctions();
    void stopCurrentAmbientSetFunctions();
    void rotateToNextActiveAmbientSet();

    /*********************************************************************
     * Groups: a named collection of Scenes/Chasers, reusable as a single
     * entry inside any Profile's Chaser list AND/OR any Ambient Set's
     * function list (that's the point - the same Group can back a
     * "Chasers" entry and an "Idle" entry at once). Two modes:
     *  - Parallel (rotate == false): every member behaves exactly as if
     *    it had been added to the containing Profile/Ambient Set
     *    directly - i.e. no different from today's plain multi-Chaser
     *    Profile, just organized/named as a unit. A Chaser member inside
     *    a Profile still steps forward on every Beat/Kick like normal.
     *  - Rotate (rotate == true): only ONE member is actually running at
     *    a time; every rotateIntervalSec (wall-clock, NOT beat-synced -
     *    deliberately, since the ask was "every 10/30/60/180 seconds",
     *    not "every N beats") the group stops the current member and
     *    starts the next one, wrapping around. A Chaser that happens to
     *    be the current member of a Rotate Group inside a Profile still
     *    steps forward on the beat while it's selected - only WHICH
     *    member is selected is timer-driven, not step advancement
     *    itself.
     * Groups have no per-member enable flag (unlike Profile/Ambient Set
     * entries) - remove a member from the Group instead of disabling it,
     * keeps the model simple. Rotation state lives in m_groupRuntime,
     * keyed by FunctionGroup::id, and is intentionally NOT persisted -
     * every time a Group (in Rotate mode) actually starts running (its
     * containing Profile/Ambient Set becomes current), it begins fresh
     * at member 0.
     *********************************************************************/
public:
    /** [{ id, name, rotate, rotateIntervalSec, members: [{id, name}] },
     *  ...] - the Group list, edited in the widget's Properties panel. */
    QVariantList groups() const;

    /** Returns the new Group's id (FunctionGroup::id, stable across
     *  reordering/deletion of OTHER groups - NOT an index into
     *  groups()) so a caller can immediately reference it, e.g. to open
     *  its member-editing panel right after creating it. */
    Q_INVOKABLE quint32 addGroup(const QString &name);
    Q_INVOKABLE void removeGroupAt(int index);
    Q_INVOKABLE void renameGroup(int index, const QString &name);
    Q_INVOKABLE void setGroupRotate(int index, bool rotate);
    Q_INVOKABLE void setGroupRotateIntervalSec(int index, int seconds);
    Q_INVOKABLE void addMemberToGroup(int groupIndex, quint32 functionId);
    Q_INVOKABLE void removeMemberFromGroup(int groupIndex, int memberIndex);

signals:
    void groupsChanged();

private:
    /** Index into m_groups for a given FunctionGroup::id, or -1. Groups
     *  are referenced by id everywhere outside this lookup (stable
     *  across list edits), never by array index. */
    int groupIndexById(quint32 groupId) const;
    QString functionOrGroupName(const FunctionRef &ref) const;
    /** True if $ref refers directly to a Show ("Super Chaser") function -
     *  used by the QML side to show a distinguishing badge, the same way
     *  isGroup already does for Groups. */
    bool isShowFunction(const FunctionRef &ref) const;

    // Shared by both Profile and Ambient Set group handling (see the
    // class comment above) - starting a member fresh (step 0 if it's a
    // Chaser) vs. just stopping whatever's running, regardless of which
    // container the Group happens to be running under right now.
    void startGroupMember(quint32 functionId, double masterIntensity);
    void stopGroupMember(quint32 functionId);

    /** One Profile-list entry's per-Beat/Kick advance, generalized over
     *  plain Chasers and Groups - see the class comment above groups().
     *  Replaces direct startOrStepChaser() calls in
     *  advanceCurrentProfileChasers(). */
    void advanceProfileEntry(const FunctionRef &ref, double masterIntensity);
    /** Stops whatever a Profile-list entry is currently running -
     *  replaces the direct per-Chaser stop loop in
     *  stopCurrentProfileChasers(). */
    void stopProfileEntry(const FunctionRef &ref);

    /** One Ambient-list entry's start/stop, generalized the same way -
     *  replaces the direct per-function start/stop in
     *  startCurrentAmbientSetFunctions()/stopCurrentAmbientSetFunctions(). */
    void startAmbientEntry(const FunctionRef &ref);
    void stopAmbientEntry(const FunctionRef &ref);

    /** Called from slotAnalysisStateChanged() (fires every audio block,
     *  independent of Beat/Kick/tempo - the wall-clock timing a Rotate
     *  Group needs) - advances every currently-running Rotate Group
     *  whose interval has elapsed. */
    void tickGroupRotations();

    /*********************************************************************
     * Super Chasers: a named bundle of "tracks" (plain Chasers/Scenes or
     * Groups, exactly the same entries a Profile's Chaser list already
     * accepts) that all run IN PARALLEL, beat/kick-advanced, for as long
     * as the Super Chaser itself is active - the Daslight-5 "Super Scene"
     * idea (multiple tracks of chasers/scenes playing together, in time
     * with the music), addable as a single unit to a Profile's Chaser
     * list or an Ambient Set, exactly like a Group already is.
     *
     * This is DELIBERATELY NOT built on QLC+'s Show/Track/ShowFunction
     * engine (which the "Effekt Editor" page's "Super Chaser" used to
     * mean, before this class existed) - a Show is a TIME-SCHEDULED
     * timeline (each track item plays at a fixed timestamp/duration,
     * independent of the music), which is the wrong engine for "keeps
     * stepping in sync with whatever's currently playing". A Show remains
     * available in the Effekt Editor as a genuinely different, legitimate
     * tool (fixed-timeline programming) - see isShowFunction() - it's
     * just no longer what "Super Chaser" refers to.
     *
     * Structurally a SuperChaser is almost identical to a MusicProfile
     * (a QVector<FunctionRef> of tracks) minus the active/rotation
     * concept - a SuperChaser is only ever active by being referenced
     * (enabled or not, like any other FunctionRef) from something that
     * IS itself active (a Profile, an Ambient Set). advanceProfileEntry()/
     * stopProfileEntry()/startAmbientEntry() all check ref.isSuperChaser
     * FIRST and, if set, simply recurse into every enabled track with the
     * exact same function - reusing the Group/plain-Chaser dispatch
     * completely unchanged, so a Chaser inside a Super Chaser's track
     * advances via the identical startOrStepChaser() call (and therefore
     * the identical Beat/Kick timing) as a Chaser placed directly in a
     * Profile. A track is never itself another Super Chaser (recursion
     * only goes one level deep - enforced by addTrackToSuperChaser()/
     * addGroupTrackToSuperChaser() only ever setting isGroup, never
     * isSuperChaser, on the FunctionRefs they create).
     *********************************************************************/
public:
    /** [{ id, name, tracks: [{id, name, enabled, isGroup}] }, ...] - the
     *  Super Chaser list, edited in the widget's Properties panel. */
    QVariantList superChasers() const;

    /** Returns the new Super Chaser's id (stable, NOT an index) so a
     *  caller can immediately reference it. */
    Q_INVOKABLE quint32 addSuperChaser(const QString &name);
    Q_INVOKABLE void removeSuperChaserAt(int index);
    Q_INVOKABLE void renameSuperChaser(int index, const QString &name);
    Q_INVOKABLE void addTrackToSuperChaser(int superChaserIndex, quint32 functionId);
    /** Same as addTrackToSuperChaser(), for an existing Group instead -
     *  groupId is FunctionGroup::id. */
    Q_INVOKABLE void addGroupTrackToSuperChaser(int superChaserIndex, quint32 groupId);
    Q_INVOKABLE void removeTrackFromSuperChaser(int superChaserIndex, int trackIndex);
    Q_INVOKABLE void setTrackEnabledInSuperChaser(int superChaserIndex, int trackIndex, bool enabled);

    /** Adds an existing Super Chaser as an entry of a Profile's Chaser
     *  list / an Ambient Set, right alongside plain Chasers and Groups -
     *  superChaserId is SuperChaser::id, NOT an index into
     *  superChasers(). */
    Q_INVOKABLE void addSuperChaserToProfile(int profileIndex, quint32 superChaserId);
    Q_INVOKABLE void addSuperChaserToAmbientSet(int setIndex, quint32 superChaserId);

signals:
    void superChasersChanged();

private:
    /** Index into m_superChasers for a given SuperChaser::id, or -1. */
    int superChaserIndexById(quint32 superChaserId) const;
    /** True if $ref refers directly to a Super Chaser - used by the QML
     *  side for the distinguishing badge, the same way isGroup/
     *  isShowFunction already do. */
    bool isSuperChaserFunction(const FunctionRef &ref) const;
    /** 0 = plain engine Function, 1 = Group, 2 = Super Chaser - the third
     *  segment of every MIDI control composite key (see syncMidiControls())
     *  that references a FunctionRef. A plain bool (isGroup ? 1 : 0, as
     *  used before Super Chasers existed) can no longer disambiguate a
     *  Group and a Super Chaser that happen to share the same numeric id
     *  (they're separate id namespaces) - this replaces every such use. */
    static int refKindCode(const FunctionRef &ref);

    /*********************************************************************
     * Super Shows: a named, ordered sequence of wall-clock "sections",
     * each simply naming one existing Profile (see the class comment
     * above profiles()) and a duration in seconds - the Daslight-5
     * "programme a timeline, drag scenes onto it" idea, but built as
     * pure ORCHESTRATION on top of the already-proven Beat/Kick-synced
     * Profile system rather than a new playback engine of its own.
     *
     * While a Super Show is running, a section boundary does nothing
     * more than call the exact same stopCurrentProfileChasers() /
     * m_currentProfileIndex = ... / startCurrentProfileChasers() dance
     * rotateToNextActiveProfile() already does - the only difference is
     * WHICH Profile it switches to and WHEN is scripted ahead of time
     * (this section's Profile, at this section's start time) instead of
     * decided live by the round-robin/bar-count rule. Everything that
     * happens *inside* a section - which Chasers step on which Beat/Kick,
     * Super Chasers recursing into their tracks, Groups rotating - is
     * completely unchanged: it's still that Profile's own already-tested
     * Beat-sync dispatch, just switched on and off on a schedule.
     *
     * This is DELIBERATELY NOT the same tool as either a plain "Show"
     * (engine Show/Track/ShowFunction, fixed-timeline playback of
     * individual Functions - see isShowFunction()) or a beat-synced
     * "Super Chaser" (multiple Chasers/Groups stepping together forever
     * while active - see the class comment above superChasers()): a
     * Super Show answers "at minute 3, stop running Profile A and start
     * running Profile B instead", nothing finer-grained than that. Build
     * the Profiles/Super Chasers themselves first (in this widget's own
     * Properties panel, exactly as before); a Super Show just scripts
     * WHEN each one takes over, and lives in the Effekt Editor tab
     * (PopupSuperShowEditor.qml) rather than this widget's own
     * Properties panel, since composing a running order is a different
     * kind of task than defining what a single Profile contains.
     *
     * Ticked from slotAnalysisStateChanged() (see tickSuperShow()),
     * the exact same audio-block heartbeat Group rotation already uses
     * for its own wall-clock timing (see the class comment above
     * groups()) - so, like Group rotation, a Super Show only actually
     * advances while audio capture is running. Consistent with the rest
     * of this widget being a live-performance tool that's always run
     * with capture on, and avoids a second, redundant QTimer.
     *********************************************************************/
public:
    /** [{ id, name, loop, sections: [{ profileId, profileName, durationSec }] },
     *  ...] - the Super Show list, edited in PopupSuperShowEditor.qml
     *  (Effekt Editor tab). profileName resolves live so a renamed
     *  Profile is reflected immediately without touching the Super Show
     *  itself. */
    QVariantList superShows() const;

    Q_INVOKABLE quint32 addSuperShow(const QString &name);
    Q_INVOKABLE void removeSuperShowAt(int index);
    Q_INVOKABLE void renameSuperShow(int index, const QString &name);
    /** true: once the last section ends, playback wraps back to the
     *  first section and keeps going (until stopSuperShow() is called
     *  explicitly). false (default): playback simply stops after the
     *  last section's duration elapses, leaving that last section's
     *  Profile running. */
    Q_INVOKABLE void setSuperShowLoop(int index, bool loop);

    /** Appends a new section at the end (start time = sum of every
     *  earlier section's duration). profileId is MusicProfile::id, NOT
     *  an index into profiles(). */
    Q_INVOKABLE void addSectionToSuperShow(int showIndex, quint32 profileId, int durationSec);
    Q_INVOKABLE void removeSectionFromSuperShow(int showIndex, int sectionIndex);
    Q_INVOKABLE void setSectionDuration(int showIndex, int sectionIndex, int durationSec);
    Q_INVOKABLE void setSectionProfile(int showIndex, int sectionIndex, quint32 profileId);

    /** Starts playback of Super Show $index from the very beginning of
     *  its first section, immediately switching the live Profile over -
     *  stops whichever OTHER Super Show (if any) was running first.
     *  Overrides/suspends the normal multi-active-Profile round-robin
     *  rotation for as long as it's running (see rotateToNextActiveProfile()) -
     *  a Super Show is a more specific instruction about what should be
     *  current right now than the generic "keep cycling" rule is. */
    Q_INVOKABLE void startSuperShow(int index);
    /** Stops playback - leaves whichever Profile the current section had
     *  selected running (does NOT stop its chasers), simply hands
     *  control of "what's current" back to the normal manual/round-robin
     *  rules. */
    Q_INVOKABLE void stopSuperShow();

    /** Index into m_superShows of whichever Super Show is currently
     *  playing, or -1 if none is. */
    int activeSuperShowIndex() const;
    /** Index into the running Super Show's sections of whichever one is
     *  currently active, or -1 if none is playing. */
    int activeSuperShowSectionIndex() const;
    /** Milliseconds since the running Super Show started (wraps modulo
     *  the total duration while looping) - for a live playhead position
     *  in PopupSuperShowEditor.qml. 0 if none is playing. */
    qint64 superShowElapsedMs() const;

signals:
    void superShowsChanged();
    /** Covers activeSuperShowIndex/activeSuperShowSectionIndex/
     *  superShowElapsedMs together - all three only ever change at the
     *  same time (a tick or a start/stop), one signal is enough. */
    void superShowPlaybackChanged();

private:
    /** Called every slotAnalysisStateChanged() block while a Super Show
     *  is playing (see the class comment above superShows()) - advances
     *  the elapsed-time reading and switches the current Profile the
     *  moment playback crosses into a new section. */
    void tickSuperShow();

    /*********************************************************************
     * MIDI/external control: every Profile/Chaser-in-Profile/Ambient
     * Set/Function-in-Set gets its own named "external control" (see
     * VCWidget::registerExternalControl()) - the same generic MIDI-
     * learn/assign/feedback-colour mechanism every other VC widget in
     * QLC+ already uses (VCButton, VCXYPad, ...), NOT a bespoke one -
     * so the widget's own Properties panel "MIDI Control" section
     * (embedding the shared ExternalControls.qml) already lists every
     * one of these by name and lets a MIDI note/CC be learned and
     * assigned to it, with full Lower/Upper/Monitor feedback colour and
     * blink configuration for controllers that support it (Akai APC
     * Mini and friends) via the existing PopupCustomFeedback dialog -
     * none of that had to be built from scratch.
     *
     * midiEditMode adds the one thing that mechanism doesn't have on
     * its own: a quick "point at the checkbox you want, then hit the
     * pad" flow, instead of hunting the right item in a flat list.
     * While on, a click on a checkbox/row in the live widget's
     * Profiles/Chasers/Idle panels calls learnMidiForControl() for that
     * item instead of toggling it - which puts the SAME learn mechanism
     * above into "waiting for the next MIDI/keyboard event, assign it to
     * (only) this control" mode. The feedback colour/blink menu itself
     * still lives in the Properties panel's list (each entry has its own
     * "customize feedback" button) - there wasn't a robust way to pop
     * that dialog automatically the instant a note is learned without
     * digging into VirtualConsole's detection-completed signal, which
     * felt like a bigger, riskier reach for this pass than it was worth.
     *********************************************************************/
public:
    bool midiEditMode() const;
    void setMidiEditMode(bool enable);

    /** Starts MIDI-learn (or keyboard-learn) for one specific control id
     *  (see syncMidiControls()) - creates a pending, not-yet-learned
     *  input source for it if one doesn't already exist, forgetting any
     *  previous mapping first so re-learning replaces rather than stacks
     *  a second source alongside the old one. */
    Q_INVOKABLE void learnMidiForControl(int controlId);

signals:
    void midiEditModeChanged();

protected:
    /** @reimp */
    void slotInputValueChanged(quint8 id, uchar value) override;
    /** @reimp */
    void updateFeedback() override;

private:
    /** Recomputes the full set of Profile/Chaser/Ambient-Set/Function
     *  entries that should have a MIDI control registered, diffs it
     *  against what's currently registered (m_midiControlKeyToId) and
     *  registers new / unregisters stale ones - existing mappings for
     *  entries that still exist are left completely alone. Call after
     *  any structural change (add/remove/rename doesn't matter - only
     *  identity matters - a Profile/Ambient Set/Group) and once after
     *  loadXML() finishes. */
    void syncMidiControls();

    /** Sends current on/off feedback for one control id - called after
     *  every toggle (both from the UI and from slotInputValueChanged()
     *  itself, so a physical pad's LED always reflects the real state
     *  regardless of what changed it) and for every registered control
     *  from updateFeedback(). */
    void sendControlFeedback(quint8 controlId, bool on);

    /** -1 if $key (see syncMidiControls()) has no MIDI control
     *  registered right now - otherwise the control id, for QML to pass
     *  straight into learnMidiForControl(). Used by profiles()/
     *  visibleChasers()/ambientSets()/visibleAmbientFunctions() to embed
     *  "midiControlId" in each entry. */
    int midiControlIdFor(const QString &key) const;

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
        // false: id is a real engine Function id. true: id is a
        // FunctionGroup::id instead - see the class comment above
        // groups(). Every place that reads a FunctionRef has to check
        // this before treating id as a Function id. No default value
        // (unlike enabled/active elsewhere in this file) - FunctionRef
        // is aggregate-initialized with braces at every construction
        // site (FunctionRef{id, enabled, isGroup}), and a default
        // member initializer here would stop that from compiling before
        // C++20.
        bool isGroup;
        // true: id is a SuperChaser::id instead of an engine Function id
        // - see the class comment above superChasers(). Given a default
        // initializer (unlike isGroup above) deliberately: aggregate
        // init with FEWER initializers than members value-initializes
        // the rest, so every EXISTING 3-argument FunctionRef{id, enabled,
        // isGroup} call site keeps compiling unchanged, with this new
        // field correctly defaulting to false there - no need to touch
        // any of them.
        bool isSuperChaser = false;
    };

    struct MusicProfile
    {
        // Stable, NOT an index - unlike Groups this predates having a
        // stable id, so existing saved projects have none; loadXML()
        // assigns one on the fly for any Profile that doesn't carry one
        // (id=0 there means "none yet", not a real id - real ids start
        // at 1, same convention as m_nextGroupId). Needed so a MIDI
        // control (see syncMidiControls()) survives Profiles being
        // added/removed/reordered elsewhere.
        quint32 id = 0;
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
    quint32 m_nextProfileId = 1;

    // Same shape as MusicProfile, for the idle/break state - see the
    // class comment above ambientSets().
    struct AmbientSet
    {
        quint32 id = 0; // see MusicProfile::id
        QString name;
        QVector<FunctionRef> functions;
        bool active = false;
    };
    QVector<AmbientSet> m_ambientSets;
    quint32 m_nextAmbientSetId = 1;
    // Index into m_ambientSets of whichever active Set is currently
    // selected to run during a break, or -1 if none are active. Unlike
    // m_currentProfileIndex (rotates on a bar count while current),
    // this only changes on rotateToNextActiveAmbientSet(), called once
    // per break ENTERED - see the class comment above ambientSets().
    int m_currentAmbientSetIndex = -1;

    // See the class comment above groups().
    struct FunctionGroup
    {
        quint32 id = 0;         // stable, NOT an index - see groupIndexById()
        QString name;
        QVector<quint32> members; // engine Function ids (Scenes/Chasers) - never other Groups
        bool rotate = false;      // false = Parallel, true = Rotate
        int rotateIntervalSec = 30;
    };
    QVector<FunctionGroup> m_groups;
    quint32 m_nextGroupId = 1; // 0 is never assigned, usable as an "invalid" sentinel if ever needed

    // Runtime-only (never persisted) rotation state for a Rotate Group
    // that's actually running right now, keyed by FunctionGroup::id.
    // Populated when a Profile/Ambient Set entry referencing the Group
    // starts, cleared when it stops - see startGroupMember()/
    // stopGroupMember()/tickGroupRotations().
    struct GroupRuntime
    {
        int currentMemberIndex = -1;
        QElapsedTimer timer;
        bool timerValid = false;
    };
    QHash<quint32, GroupRuntime> m_groupRuntime;

    // See the class comment above superChasers().
    struct SuperChaser
    {
        quint32 id = 0;             // stable, NOT an index - see superChaserIndexById()
        QString name;
        QVector<FunctionRef> tracks; // plain Chasers/Scenes or Groups - never another SuperChaser
    };
    QVector<SuperChaser> m_superChasers;
    quint32 m_nextSuperChaserId = 1;

    // See the class comment above superShows().
    struct SuperShowSection
    {
        quint32 profileId = 0; // MusicProfile::id to switch to at this section
        int durationSec = 60;  // how long this section runs before the next one begins
    };
    struct SuperShow
    {
        quint32 id = 0;        // stable, NOT an index (mirrors MusicProfile::id)
        QString name;
        QVector<SuperShowSection> sections; // in running order
        bool loop = false;
    };
    QVector<SuperShow> m_superShows;
    quint32 m_nextSuperShowId = 1;

    // Playback state - runtime only, never persisted (a project always
    // loads with every Super Show stopped, same as nothing being
    // "current" until capture picks something).
    int m_activeSuperShowIndex = -1;
    int m_activeSuperShowSectionIndex = -1;
    // Wall-clock elapsed reading, started fresh on every startSuperShow()
    // call - see tickSuperShow().
    QElapsedTimer m_superShowTimer;
    bool m_superShowTimerValid = false;

    /*********************************************************************
     * MIDI/external control - see the class comment above midiEditMode()
     *********************************************************************/
    // Stable composite key (e.g. "profile:3", "chaser:3:1:0") -> the
    // quint8 id actually registered via registerExternalControl(). The
    // reverse (id -> key) lookup needed by slotInputValueChanged() is
    // just m_midiControlKeyToId inverted on demand - not kept as a
    // second map since it only needs to run once per MIDI event, not
    // once per frame.
    QHash<QString, quint8> m_midiControlKeyToId;
    // 0..254 - 255 (VCWIDGET_AUTODETECT_INPUT_ID) is reserved by the
    // base class as the "not yet assigned" sentinel. Never reused once
    // handed out (even after the owning item is deleted and
    // syncMidiControls() unregisters it) - simpler and safer than a
    // free-list, at the cost of exhausting the 255-control budget
    // somewhat faster on a project that creates and deletes a lot of
    // Profiles/Chasers/Sets/Groups over its lifetime. Comfortably enough
    // for any realistic show either way.
    int m_nextMidiControlId = 0;
    bool m_midiEditMode = false; // transient UI state, not persisted
};

#endif // VCMUSICREACTIVE_H
