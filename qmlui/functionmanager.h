/*
  Q Light Controller Plus
  functionmanager.h

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

#ifndef FUNCTIONMANAGER_H
#define FUNCTIONMANAGER_H

#include <QStringList>
#include <QQuickView>
#include <QQuickItem>
#include <QJsonArray>
#include <QObject>
#include <QColor>
#include <QSet>

#include "scenevalue.h"
#include "treemodel.h"

class Doc;
class Fixture;
class Function;
class Scene;
class Chaser;
class SceneEditor;
class FunctionEditor;
class GenericDMXSource;
class QNetworkAccessManager;
class QNetworkReply;
class QJsonObject;

typedef struct
{
    quint32 m_fID;
    QQuickItem *m_item;
} selectedFunction;

class FunctionManager final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(quint32 startupFunctionID READ startupFunctionID WRITE setStartupFunctionID NOTIFY startupFunctionIDChanged)

    Q_PROPERTY(QVariant functionsList READ functionsList NOTIFY functionsListChanged)
    Q_PROPERTY(int functionsFilter READ functionsFilter CONSTANT)
    Q_PROPERTY(QString searchFilter READ searchFilter WRITE setSearchFilter NOTIFY searchFilterChanged)
    Q_PROPERTY(int selectedFunctionCount READ selectedFunctionCount NOTIFY selectedFunctionCountChanged)
    Q_PROPERTY(int selectedFolderCount READ selectedFolderCount NOTIFY selectedFolderCountChanged)
    Q_PROPERTY(bool isEditing READ isEditing NOTIFY isEditingChanged)
    Q_PROPERTY(int viewPosition READ viewPosition WRITE setViewPosition NOTIFY viewPositionChanged)

    Q_PROPERTY(int sceneCount READ sceneCount NOTIFY sceneCountChanged)
    Q_PROPERTY(int chaserCount READ chaserCount NOTIFY chaserCountChanged)
    Q_PROPERTY(int sequenceCount READ sequenceCount NOTIFY sequenceCountChanged)
    Q_PROPERTY(int efxCount READ efxCount NOTIFY efxCountChanged)
    Q_PROPERTY(int collectionCount READ collectionCount NOTIFY collectionCountChanged)
    Q_PROPERTY(int rgbMatrixCount READ rgbMatrixCount NOTIFY rgbMatrixCountChanged)
    Q_PROPERTY(int scriptCount READ scriptCount NOTIFY scriptCountChanged)
    Q_PROPERTY(int showCount READ showCount NOTIFY showCountChanged)
    Q_PROPERTY(int audioCount READ audioCount NOTIFY audioCountChanged)
    Q_PROPERTY(int videoCount READ videoCount NOTIFY videoCountChanged)

    Q_PROPERTY(QStringList audioExtensions READ audioExtensions CONSTANT)
    Q_PROPERTY(QStringList pictureExtensions READ pictureExtensions CONSTANT)
    Q_PROPERTY(QStringList videoExtensions READ videoExtensions CONSTANT)

    Q_PROPERTY(bool previewEnabled READ previewEnabled WRITE setPreviewEnabled NOTIFY previewEnabledChanged)
    Q_PROPERTY(bool scenePreviewEnabled READ scenePreviewEnabled WRITE setScenePreviewEnabled NOTIFY scenePreviewEnabledChanged)

public:
    FunctionManager(QQuickView *view, Doc *doc, QObject *parent = 0);
    ~FunctionManager();

    quint32 startupFunctionID() const;
    void setStartupFunctionID(quint32 fid);

signals:
    void startupFunctionIDChanged();
    void itemClicked(int itemType);

    /*********************************************************************
     * Functions
     *********************************************************************/
public:
    /** Read only property to expose the function tree to the QML UI */
    QVariant functionsList();

    Q_INVOKABLE quint32 nextFunctionId() const;

    /** Get a list of Functions that use $fid */
    Q_INVOKABLE QVariantList usageList(quint32 fid);

    /** Get a list of the currently selected Function IDs, suitable to be used in QML */
    Q_INVOKABLE QVariantList selectedFunctionsID();

    /** Get a list of the currently selected item names, suitable to be used in QML */
    Q_INVOKABLE QStringList selectedItemNames();

    /** Enable/disable the display of a Function type in the functions tree */
    Q_INVOKABLE void setFunctionFilter(quint32 filter, bool enable);
    int functionsFilter() const;

    /** Get/Set a string to filter Function names */
    QString searchFilter() const;
    void setSearchFilter(QString searchFilter);

    /** Create a new Function with the specified $type
      * If the optional fixturesList is provided, fixture IDs
      * will be added to the Function where possible.
      */
    Q_INVOKABLE quint32 createFunction(int type, QVariantList fixturesList = QVariantList());

    /** Create a new Audio/Video Function for each
     *  file path provided in fileList.
     */
    Q_INVOKABLE quint32 createAudioVideoFunction(int type, QStringList fileList = QStringList());

    /** Return a reference to a Function with the specified $id */
    Q_INVOKABLE Function *getFunction(quint32 id);

    /** Return the associated qrc icon resource for the specified Function $type */
    Q_INVOKABLE QString functionIcon(int type);

    Q_INVOKABLE QString functionPath(quint32 id);

    /** Enable/disable the Function preview feature */
    bool previewEnabled() const;
    void setPreviewEnabled(bool enable);

    /** Enable/disable the live preview of the channel values being edited
     *  on a Scene/Sequence. When enabled, the edited Scene (or the Sequence
     *  bound Scene) is started, so channel changes are sent to the output */
    bool scenePreviewEnabled() const;
    void setScenePreviewEnabled(bool enable);

    /** Add $fID to the list of the currently selected Function IDs,
     *  considering $multiSelection as an append/replace action */
    Q_INVOKABLE void selectFunctionID(quint32 fID, bool multiSelection);

    /** Get the QML resource for a Function editor that can edit $funcID */
    Q_INVOKABLE QString getEditorResource(int funcID);

    /** Set $fID as the current Function ID being edited */
    Q_INVOKABLE void setEditorFunction(quint32 fID, bool requestUI, bool back);

    /** Return a reference of the currently open Function editor */
    FunctionEditor *currentEditor() const;

    /** Returns if the UI is editing a Function */
    bool isEditing() const;

    void deleteFunction(quint32 fid);

    /** Delete the list of Function IDs in $IDList. This happens AFTER a popup confirmation */
    Q_INVOKABLE void deleteFunctions(QVariantList IDList);

    /** Move the currently selected Function to the specified $newPath */
    Q_INVOKABLE void moveFunctions(QString newPath);

    /** Clone the currently selected Functions */
    Q_INVOKABLE void cloneFunctions();

    /** Generic method to delete a list of item IDs specified in $list.
     *  This is used from within a Function editor and items can be of any type
     *  such as Functions, Fixtures, etc. as long as they have an ID.
     *  This happens AFTER a popup confirmation */
    Q_INVOKABLE void deleteEditorItems(QVariantList list);

    /** Ask the currently open Function editor to delete its selected items.
     *  The editor raises its own confirmation popup, so this happens
     *  BEFORE any actual deletion.
     *  Returns true if an editor handled the request */
    bool deleteCurrentEditorItems();

    /** Specific method to delete fixtures from the currently edited Sequence.
     *  This happens AFTER a popup confirmation */
    Q_INVOKABLE void deleteSequenceFixtures(QVariantList list);

    /** Rename the currently selected items (functions and/or folders)
     *  with the provided $newName.
     *  If $numbering is true, then $startNumber and $digits will compose
     *  a progress number following $newName */
    Q_INVOKABLE bool renameSelectedItems(QString newName, bool numbering, int startNumber, int digits);

    /** Returns the number of the currently selected Functions */
    int selectedFunctionCount() const;

    int sceneCount() const { return m_sceneCount; }
    int chaserCount() const { return m_chaserCount; }
    int sequenceCount() const { return m_sequenceCount; }
    int efxCount() const { return m_efxCount; }
    int collectionCount() const { return m_collectionCount; }
    int rgbMatrixCount() const { return m_rgbMatrixCount; }
    int scriptCount() const { return m_scriptCount; }
    int showCount() const { return m_showCount; }
    int audioCount() const { return m_audioCount; }
    int videoCount() const { return m_videoCount; }

    QStringList audioExtensions() const;
    QStringList pictureExtensions() const;
    QStringList videoExtensions() const;

    void setViewPosition(int viewPosition);
    int viewPosition() const;

protected:
    quint32 addFunctiontoDoc(Function *func, QString name, bool select);
    void addFunctionTreeItem(Function *func);
    void updateFunctionsTree();
    void clearTree();
    void moveFunction(quint32 fID, QString newPath);
    void storeExpandedPaths();
    void restoreExpandedPaths();

signals:
    void functionsListChanged();
    void searchFilterChanged();
    void sceneCountChanged();
    void chaserCountChanged();
    void sequenceCountChanged();
    void efxCountChanged();
    void collectionCountChanged();
    void rgbMatrixCountChanged();
    void scriptCountChanged();
    void showCountChanged();
    void audioCountChanged();
    void videoCountChanged();
    void selectedFunctionCountChanged(int count);
    void previewEnabledChanged();
    void scenePreviewEnabledChanged();
    void isEditingChanged(bool editing);
    void viewPositionChanged(int viewPosition);

public slots:
    void slotDocLoaded();
    void slotFunctionAdded(quint32 fid);

private:
    /** Reference of the QML view */
    QQuickView *m_view;
    /** Reference of the project workspace */
    Doc *m_doc;
    /** Reference to the Functions tree model */
    TreeModel *m_functionTree;
    /** The QML ListView position in pixel for state restoring */
    int m_viewPosition;

    /** Flag that hold if Functions preview is enabled or not */
    bool m_previewEnabled;

    /** Flag that holds if the live preview of the edited Scene/Sequence
     *  channel values is enabled or not */
    bool m_scenePreviewEnabled;

    /** List of the Function IDs currently selected
     *  and previewed, if preview is enabled */
    QVariantList m_selectedIDList;

    quint32 m_filter;
    QString m_searchFilter;

    int m_sceneCount, m_chaserCount, m_sequenceCount, m_efxCount;
    int m_collectionCount, m_rgbMatrixCount, m_scriptCount;
    int m_showCount, m_audioCount, m_videoCount;

    FunctionEditor *m_currentEditor;
    FunctionEditor *m_sceneEditor;

    /*********************************************************************
     * Folders
     *********************************************************************/
public:
    /** Select a folder with the given $path */
    Q_INVOKABLE void selectFolder(QString path, bool multiSelection);

    /** Return the number of currently selected folders */
    int selectedFolderCount() const;

    /** Change the path of an existing folder and all its children */
    Q_INVOKABLE void setFolderPath(QString oldAbsPath, QString newPath, bool isRelative);

    /** Create an empty folder with path starting from the currently
     *  selected item */
    Q_INVOKABLE bool createFolder(QString folderName);

    /** Delete the currently selected folders. If a folder is not empty,
     *  delete also all the sub items in it */
    Q_INVOKABLE void deleteSelectedFolders();

signals:
    void selectedFolderCountChanged(int count);

private:
    /** List of the folder paths that don't include any Function yet */
    QStringList m_emptyFolderList;
    /** List of the folders currently selected */
    QStringList m_selectedFolderList;
    /** List of the expanded folder paths to restore after tree rebuild */
    QStringList m_expandedPaths;
    /** Path of the currently edited Function, used to keep its folders expanded */
    QString m_editorFunctionPath;

    /*********************************************************************
     * DMX values (dumping and Scene editor)
     *********************************************************************/
public:

    /** Return the currently set channel values */
    QList <SceneValue> dumpValues() const;

    /** Reset the currently set channel values */
    void resetDumpValues();

    void dumpDmxValues(QList<SceneValue> dumpValues, QList<quint32> selectedFixtures,
                       quint32 channelMask, QString sceneName, quint32 sceneID, bool nonZeroOnly);

    /** Dump DMX values provided by $dumpValues, filtered by the provided $selectedFixtures
     *  and the provided $channelMask.
     *  The new Scene will be named with $name if not empty, otherwise with an autogenerated name */
    void dumpOnNewScene(QList<SceneValue> dumpValues, QList<quint32> selectedFixtures,
                        quint32 channelMask, QString name);

    /** Dump DMX values provided by $dumpValues, filtered by the provided $selectedFixtures
     *  and the provided $channelMask. Values are dumped on an existing Scene with $sceneID */
    void dumpOnScene(QList<SceneValue> dumpValues, QList<quint32> selectedFixtures,
                     quint32 channelMask, quint32 sceneID);

    /** Check if the current editor accepts Scene values */
    bool acceptsSceneValues();

    Q_INVOKABLE void setChannelValue(quint32 fxID, quint32 channel, uchar value);

protected:
    quint32 getChannelTypeMask(quint32 fxID, quint32 channel);

    /*********************************************************************
     * AI Effect Generator (Ollama)
     *********************************************************************/
public:
    Q_PROPERTY(bool aiBusy READ aiBusy NOTIFY aiBusyChanged)
    Q_PROPERTY(QString aiStatus READ aiStatus NOTIFY aiStatusChanged)
    Q_PROPERTY(QStringList aiAvailableModels READ aiAvailableModels NOTIFY aiAvailableModelsChanged)
    Q_PROPERTY(QString aiSelectedModel READ aiSelectedModel WRITE setAiSelectedModel NOTIFY aiSelectedModelChanged)
    Q_PROPERTY(QStringList aiPresetPrompts READ aiPresetPrompts CONSTANT)
    Q_PROPERTY(QString aiServerUrl READ aiServerUrl WRITE setAiServerUrl NOTIFY aiServerUrlChanged)

    bool aiBusy() const;
    QString aiStatus() const;
    QStringList aiAvailableModels() const;
    QString aiSelectedModel() const;
    void setAiSelectedModel(QString model);
    QStringList aiPresetPrompts() const;
    QString aiServerUrl() const;
    void setAiServerUrl(QString url);

    /** Ask Ollama's model list. Populates aiAvailableModels asynchronously. */
    Q_INVOKABLE void aiRefreshModels();

    /** Analyze the current fixture inventory + 2D layout, send $prompt to Ollama,
     *  and create the resulting Scene/Chaser Function(s) in the Doc. */
    Q_INVOKABLE void aiGenerate(QString prompt);

    /** Instantly create a curated "starter pack" of preset effects (solid colours,
     *  an alternating-colour scene, an on/off chase, a rainbow chase, a random-dots
     *  chase, a strobe, and - if the rig has any pan/tilt fixture - a mirrored
     *  moving-head sweep), for when the user has no specific idea yet. Runs the
     *  exact same pattern builders aiGenerate() uses, just with fixed presets
     *  instead of an LLM classification, so it's instant and doesn't depend on
     *  Ollama being reachable. */
    Q_INVOKABLE void aiQuickSetup();

signals:
    void aiBusyChanged();
    void aiStatusChanged();
    void aiAvailableModelsChanged();
    void aiSelectedModelChanged();
    void aiServerUrlChanged();
    /** Emitted when a generation request completes successfully. $summary is a
     *  short human readable description of what was created, for the UI to show. */
    void aiFinished(QString summary);
    /** Emitted when a generation request fails, with a human readable $error */
    void aiFailed(QString error);

private:
    void setAiBusy(bool busy);
    void setAiStatus(QString status);

    /** Build a compact JSON description of every fixture in the Doc: id, name,
     *  channel-group capabilities (colour/intensity/pan/tilt) and 2D designer
     *  position (when available via MonitorProperties), for the LLM's context. */
    QJsonArray aiBuildFixtureContext() const;

    /** POST $userPrompt (plus the fixture context and schema instructions embedded
     *  in the system prompt) to Ollama's /api/chat, expecting a JSON object back. */
    void aiRequestPlan(const QString &userPrompt);

    /** Parse and apply an LLM-produced "plan" JSON object: dispatches to the
     *  matching pattern builder and creates the resulting Function(s) in the Doc. */
    void aiApplyPlan(const QJsonObject &plan);

    /* Pattern builders. Each returns a short human-readable summary on success
     * and throws (via a local flag / empty QString convention documented at each
     * call site) - see functionmanager_ai.cpp for the actual error handling. */
    QString aiBuildSolidColor(const QJsonObject &plan, const QList<Fixture*> &fixtures);
    QString aiBuildAlternateColor(const QJsonObject &plan, const QList<Fixture*> &fixtures);
    QString aiBuildChaseOnOff(const QJsonObject &plan, const QList<Fixture*> &fixtures);
    QString aiBuildRainbowChase(const QJsonObject &plan, const QList<Fixture*> &fixtures);
    QString aiBuildRandomDots(const QJsonObject &plan, const QList<Fixture*> &fixtures);
    QString aiBuildStrobe(const QJsonObject &plan, const QList<Fixture*> &fixtures);
    QString aiBuildMirrorMovement(const QJsonObject &plan, const QList<Fixture*> &fixtures);
    QString aiBuildCustomSteps(const QJsonObject &plan, const QList<Fixture*> &fixtures);

    /** Resolve the fixture list a plan applies to: either an explicit "fixtureIds"
     *  array in $params, or every fixture in the Doc if absent/empty. */
    QList<Fixture*> aiResolveFixtures(const QJsonObject &params) const;

    /** Sort $fixtures left-to-right by their 2D designer x position, when known
     *  (via MonitorProperties); falls back to id order for any fixture without
     *  a known position. Used so "jedes erste/zweite" reads spatially. */
    void aiSortFixturesSpatially(QList<Fixture*> &fixtures) const;

    /** Apply an RGB colour to $fixture inside $scene, using its RGB channels if
     *  present, falling back to CMY, falling back to just full intensity. */
    void aiSetFixtureColor(Scene *scene, Fixture *fixture, QColor color);
    /** Set $fixture's master/dimmer intensity (0-255) inside $scene; if the
     *  fixture has no dedicated intensity channel, drives its RGB instead
     *  (full white/off) so "on/off" still has a visible effect. */
    void aiSetFixtureIntensity(Scene *scene, Fixture *fixture, uchar value);
    /** Point $fixture's pan/tilt to a fraction (0.0-1.0) of its own physical
     *  pan/tilt range, inside $scene. */
    void aiSetFixturePosition(Scene *scene, Fixture *fixture, float panFraction, float tiltFraction);
    /** Shared by aiSetFixturePosition() and the Moving Editor's live preview:
     *  the actual fraction->degrees->DMX channel/value computation, without
     *  committing it anywhere. */
    static QList<SceneValue> aiComputePositionValues(Fixture *fixture, float panFraction, float tiltFraction);

    /** Create a new Scene named "$baseName" (auto-numbered), owned by the Doc,
     *  same lifecycle as a user-created Scene (Tardis undo, tree, ownership). */
    Scene *aiCreateScene(const QString &baseName);
    /** Create a new Chaser named "$baseName" referencing $stepFunctionIds in
     *  order, with the given per-step timing, looping. */
    Chaser *aiCreateChaser(const QString &baseName, const QList<quint32> &stepFunctionIds,
                            uint fadeInMs, uint holdMs, uint fadeOutMs);

    /** Parse a colour name ("red", "blue", "warm white", "#ff8800", ...) into a QColor.
     *  Falls back to white if unrecognised. */
    static QColor aiParseColor(const QString &name);

    QNetworkAccessManager *m_aiNetworkManager;
    bool m_aiBusy;
    QString m_aiStatus;
    QStringList m_aiAvailableModels;
    QString m_aiSelectedModel;
    QString m_aiServerUrl;
    QString m_aiPendingUserPrompt;

    /*********************************************************************
     * Effect Editor ("Super Scenes" / "Super Chasers", Daslight-style)
     *********************************************************************/
    /*
     * A dedicated creative page, separate from the Virtual Console/live
     * control area (mirroring how Daslight 5 keeps its scene/chaser editor
     * apart from its touchscreen page) - columns of Groups (a Group here is
     * just an existing Function folder, nothing new persisted), each
     * holding "Super Scenes" (plain Chasers) and "Super Chasers" (Show
     * functions - QLC+'s existing multi-track timeline engine already does
     * exactly what Daslight calls a Super Scene: several tracks playing in
     * parallel, each a sequence of items over time). Deep editing is
     * delegated to the existing, proven editors (ChaserEditor via the
     * normal Fixtures & Functions context, ShowManager via its own
     * top-level tab) - this page is the organizational/creation layer on
     * top, plus quick play/stop and the AI generator entry point.
     */
public:
    /** All Chaser ("Super Scene") and Show ("Super Chaser") functions,
     *  grouped by their folder path for the column view. Each group is
     *  {path, label, scenes:[{id,name,type,typeLabel,iconSource,running,
     *  detail}]}; the path "" group (label "(Ohne Gruppe)") always comes
     *  first, further groups alphabetically. */
    Q_INVOKABLE QVariantList effectGroups();

    /** Create a new empty group (Function folder). No-op if it already exists. */
    Q_INVOKABLE bool effectCreateGroup(QString name);
    /** Rename group $oldPath to $newName (both function folders and their
     *  path are updated, same as the Fixtures & Functions folder rename). */
    Q_INVOKABLE bool effectRenameGroup(QString oldPath, QString newName);
    /** Delete an empty group. Returns false (no-op) if it still has any
     *  Super Scene/Chaser in it - move them out first, nothing is deleted
     *  implicitly here. */
    Q_INVOKABLE bool effectDeleteGroup(QString path);

    /** Create a new "Super Scene" (a plain Chaser) inside $groupPath
     *  ("" = ungrouped) and return its Function id. */
    Q_INVOKABLE quint32 effectCreateSuperScene(QString groupPath, QString name);
    /** Create a new "Super Chaser" (a Show, QLC+'s multi-track timeline)
     *  inside $groupPath and return its Function id. */
    Q_INVOKABLE quint32 effectCreateSuperChaser(QString groupPath, QString name);

    /** Move an existing Chaser/Show $id into $groupPath ("" = ungrouped). */
    Q_INVOKABLE void effectMoveFunctionToGroup(quint32 id, QString groupPath);
    /** Delete a Super Scene/Chaser entirely. */
    Q_INVOKABLE void effectDeleteFunction(quint32 id);
    /** Start $id if it isn't running, stop it otherwise - quick preview
     *  from the card itself, no editor needed. */
    Q_INVOKABLE void effectToggleRun(quint32 id);
    /** True while $id is currently running - for the card's play/stop icon. */
    Q_INVOKABLE bool effectIsRunning(quint32 id) const;

    /*********************************************************************
     * Moving Head visual editor
     *********************************************************************/
    /*
     * QLC+'s standard way of building a moving-head path is either a raw
     * multi-step Scene/Chaser edited channel-by-channel, or an EFX function
     * limited to a few fixed geometric patterns - neither gives a direct,
     * click-where-you-want-it visual workflow. This adds exactly that: an
     * XY pad (0.0-1.0 fractions of each fixture's own real pan/tilt range,
     * reusing the AI generator's aiSetFixturePosition()) where every click
     * becomes a keyframe, with instant live preview via the same
     * setChannelValue()/GenericDMXSource dump mechanism the DMX dump
     * feature already uses - then "Erstellen" turns the captured path into
     * a real Chaser via the same aiCreateScene()/aiCreateChaser() helpers
     * the AI generator uses, so it's a completely ordinary, editable Chaser
     * afterwards, not a special/opaque function type.
     */
public:
    /** Pan/tilt-capable fixtures only, for the Moving Editor's picker:
     *  [{id,name,hasPan,hasTilt}, ...]. */
    Q_INVOKABLE QVariantList movingEditorFixtures() const;
    /** Instantly move $fixtureIds to (panFraction,tiltFraction) (each
     *  0.0-1.0 of that fixture's own physical range) via the live DMX dump
     *  mechanism, for direct visual feedback while building a path. Call
     *  movingEditorClearPreview() when the editor closes/cancels. */
    Q_INVOKABLE void movingEditorPreview(QVariantList fixtureIds, qreal panFraction, qreal tiltFraction);
    /** Clear any live preview values set by movingEditorPreview(). */
    Q_INVOKABLE void movingEditorClearPreview();
    /** Build a Chaser named $name in $groupPath from the captured path:
     *  $points is [{pan,tilt,holdMs,fadeMs}, ...], one Chaser step per
     *  point, in order - same step count regardless of $deviceOffsetSteps
     *  below. If $mirrorFixtureIds is non-empty, those fixtures get the
     *  same tilt but a horizontally mirrored pan (1.0-pan) at every point -
     *  for symmetric left/right moving-head pairs. Returns the new
     *  Chaser's id, or invalidId() on failure (e.g. no pan/tilt-capable
     *  fixture in either list, or an empty path).
     *
     *  $deviceOffsetSteps: 0 (default) means every fixture shows the same
     *  point at the same step, exactly as before. A positive value staggers
     *  each fixture's read position in the (cyclic) point list by its own
     *  index in $fixtureIds times this many steps - fixture 0 reads
     *  points[step], fixture 1 reads points[step - offset], fixture 2
     *  points[step - 2*offset], wrapping around - so as the Chaser loops,
     *  the same path visibly ripples down the fixture line one fixture at
     *  a time, the classic RGB-Matrix "wave"/"chase" look, built from an
     *  arbitrary hand-drawn path instead of a fixed algorithm. Mirrored
     *  fixtures are staggered the same way, counted separately within
     *  $mirrorFixtureIds. */
    Q_INVOKABLE quint32 movingEditorCreate(QString groupPath, QString name, QVariantList fixtureIds,
                                            QVariantList mirrorFixtureIds, QVariantList points,
                                            int deviceOffsetSteps = 0);

private:
    QVariantMap effectFunctionMap(Function *f) const;
    /** All group (folder) paths that currently contain at least one
     *  Chaser/Show, used by effectDeleteGroup()'s emptiness check. */
    QSet<QString> effectNonEmptyGroupPaths() const;

    /** Lazily created, used only for the Moving Editor's live preview -
     *  a plain GenericDMXSource (see sceneeditor.cpp/contextmanager.cpp for
     *  the same, already-established pattern), independent of whatever
     *  Function editor may or may not currently be open. */
    GenericDMXSource *m_movingEditorSource;
};

#endif // FUNCTIONMANAGER_H
