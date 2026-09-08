/*
  Q Light Controller Plus
  functionmanager_effecteditor.cpp

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

/*
 * "Effect Editor" (Super Scenes / Super Chasers, Daslight-5-style) and the
 * Moving Head visual editor - see the class comments in functionmanager.h
 * for the design rationale. Both sections deliberately build on top of
 * existing, proven machinery rather than inventing new persistence:
 *   - "Groups" are plain Function folders (Function::path()/setPath()),
 *     the exact same mechanism the Fixtures & Functions tree already uses.
 *   - "Super Chasers" are plain Show functions - QLC+'s existing
 *     multi-track timeline engine already does parallel/sequential
 *     playback across tracks, which is exactly what a Daslight "Super
 *     Scene" is. Deep editing of one is delegated to the existing
 *     ShowManager (its own top-level tab); this page only creates/
 *     organizes/quick-plays them.
 *   - "Super Scenes" are plain Chasers, deep-edited via the existing
 *     ChaserEditor (via the normal Fixtures & Functions context).
 *   - The Moving Editor reuses the AI generator's own position/Chaser
 *     helpers (aiComputePositionValues/aiCreateScene/aiSetFixturePosition)
 *     for actually building the result, and GenericDMXSource (the same
 *     mechanism SceneEditor/ContextManager already use) for live preview.
 */

#include <QVariantMap>
#include <QVariantList>

#include "functionmanager.h"
#include "genericdmxsource.h"
#include "chaserstep.h"
#include "treemodel.h"
#include "fixture.h"
#include "chaser.h"
#include "scene.h"
#include "show.h"
#include "doc.h"
#include "app.h"

/*****************************************************************************
 * Groups (Function folders) + Super Scene/Chaser listing
 *****************************************************************************/

QVariantMap FunctionManager::effectFunctionMap(Function *f) const
{
    QVariantMap map;
    if (f == nullptr)
        return map;

    map.insert("id", f->id());
    map.insert("name", f->name());
    map.insert("running", f->isRunning());

    if (f->type() == Function::ShowType)
    {
        map.insert("isShow", true);
        // NOT "Super Chaser" - that name now belongs to VCMusicReactive's
        // own beat-synced multi-track concept (see the class comment
        // above VCMusicReactive::superChasers()). A Show is a genuinely
        // different, still useful tool (a fixed, pre-programmed timeline,
        // not reactive to the music) - labelled plainly as what it is.
        map.insert("typeLabel", tr("Show"));
        Show *show = qobject_cast<Show*>(f);
        int trackCount = show != nullptr ? show->tracks().count() : 0;
        map.insert("detail", tr("%1 Timelines").arg(trackCount));
    }
    else
    {
        map.insert("isShow", false);
        map.insert("typeLabel", tr("Super Szene"));
        Chaser *ch = qobject_cast<Chaser*>(f);
        int steps = ch != nullptr ? ch->stepsCount() : 0;
        map.insert("detail", tr("%1 Schritte").arg(steps));
    }

    return map;
}

QSet<QString> FunctionManager::effectNonEmptyGroupPaths() const
{
    QSet<QString> paths;
    for (Function *f : m_doc->functionsByType(Function::ChaserType))
        paths.insert(f->path(true));
    for (Function *f : m_doc->functionsByType(Function::ShowType))
        paths.insert(f->path(true));
    paths.remove(QString());
    return paths;
}

QVariantList FunctionManager::effectGroups()
{
    QMap<QString, QVariantList> byPath;

    for (Function *f : m_doc->functionsByType(Function::ChaserType))
        byPath[f->path(true)].append(effectFunctionMap(f));
    for (Function *f : m_doc->functionsByType(Function::ShowType))
        byPath[f->path(true)].append(effectFunctionMap(f));

    QVariantList result;

    // the "ungrouped" bucket always comes first, even if empty
    QVariantMap ungrouped;
    ungrouped.insert("path", QString());
    ungrouped.insert("label", tr("(Ohne Gruppe)"));
    ungrouped.insert("scenes", byPath.value(QString()));
    ungrouped.insert("removable", false);
    result << ungrouped;
    byPath.remove(QString());

    for (auto it = byPath.constBegin(); it != byPath.constEnd(); ++it)
    {
        QVariantMap g;
        g.insert("path", it.key());
        g.insert("label", it.key());
        g.insert("scenes", it.value());
        g.insert("removable", true);
        result << g;
    }

    // empty groups the user already created (via effectCreateGroup()) but
    // hasn't put a Super Scene/Chaser into yet - these have no Functions at
    // all yet, so they never show up in the functionsByType() loops above
    for (const QString &emptyPath : m_emptyFolderList)
    {
        if (byPath.contains(emptyPath))
            continue;

        bool alreadyListed = false;
        for (const QVariant &gv : result)
        {
            if (gv.toMap().value("path").toString() == emptyPath)
            {
                alreadyListed = true;
                break;
            }
        }
        if (alreadyListed)
            continue;

        QVariantMap g;
        g.insert("path", emptyPath);
        g.insert("label", emptyPath);
        g.insert("scenes", QVariantList());
        g.insert("removable", true);
        result << g;
    }

    return result;
}

bool FunctionManager::effectCreateGroup(QString name)
{
    name = name.trimmed();
    if (name.isEmpty())
        return false;

    // the Effect Editor's groups are always flat/top-level, so the group's
    // path IS just its name - no tree-selection-based base path needed
    // (unlike the generic createFolder(), which nests under whatever
    // happens to be selected in the Fixtures & Functions tree)
    if (m_emptyFolderList.contains(name))
        return false;

    const QString lowerName = name.toLower();
    for (Function *f : m_doc->functions())
        if (f->path(true).toLower() == lowerName)
            return false;

    m_emptyFolderList.append(name);

    QVariantList params;
    params.append(QVariant()); // classRef
    params.append(App::FolderDragItem); // type
    m_functionTree->addItem(name, params, QString(), TreeModel::EmptyNode | TreeModel::Expanded);

    return true;
}

bool FunctionManager::effectRenameGroup(QString oldPath, QString newName)
{
    oldPath = oldPath.trimmed();
    newName = newName.trimmed();
    if (oldPath.isEmpty() || newName.isEmpty() || oldPath == newName)
        return false;

    const QString lowerNew = newName.toLower();
    if (m_emptyFolderList.contains(newName))
        return false;
    for (Function *f : m_doc->functions())
        if (f->path(true).toLower() == lowerNew)
            return false;

    setFolderPath(oldPath, newName, true);

    int idx = m_emptyFolderList.indexOf(oldPath);
    if (idx >= 0)
        m_emptyFolderList[idx] = newName;

    return true;
}

bool FunctionManager::effectDeleteGroup(QString path)
{
    path = path.trimmed();
    if (path.isEmpty())
        return false; // can't delete the implicit "ungrouped" bucket

    if (effectNonEmptyGroupPaths().contains(path))
        return false; // refuse - move the Super Scenes/Chasers out first

    if (!m_emptyFolderList.contains(path))
        return false;

    m_emptyFolderList.removeAll(path);
    m_functionTree->removeItem(path);

    return true;
}

quint32 FunctionManager::effectCreateSuperScene(QString groupPath, QString name)
{
    if (name.trimmed().isEmpty())
        name = tr("Super Szene");

    Chaser *chaser = new Chaser(m_doc);
    chaser->setDuration(1000);

    quint32 id = addFunctiontoDoc(chaser, name, false);
    if (id == Function::invalidId())
        return Function::invalidId();

    m_chaserCount++;
    emit chaserCountChanged();

    groupPath = groupPath.trimmed();
    if (!groupPath.isEmpty())
        moveFunction(id, groupPath);

    return id;
}

quint32 FunctionManager::effectCreateSuperChaser(QString groupPath, QString name)
{
    // "Super Chaser" now means VCMusicReactive's own beat-synced concept
    // (see functionOrGroupName() above) - this creates a plain Show, so
    // it's named/labelled as that instead, even though the method itself
    // keeps its established internal name.
    if (name.trimmed().isEmpty())
        name = tr("Show");

    Show *show = new Show(m_doc);

    quint32 id = addFunctiontoDoc(show, name, false);
    if (id == Function::invalidId())
        return Function::invalidId();

    m_showCount++;
    emit showCountChanged();

    groupPath = groupPath.trimmed();
    if (!groupPath.isEmpty())
        moveFunction(id, groupPath);

    return id;
}

void FunctionManager::effectMoveFunctionToGroup(quint32 id, QString groupPath)
{
    if (m_doc->function(id) == nullptr)
        return;

    moveFunction(id, groupPath.trimmed());
}

void FunctionManager::effectDeleteFunction(quint32 id)
{
    QVariantList ids;
    ids << QVariant(id);
    deleteFunctions(ids);
}

void FunctionManager::effectToggleRun(quint32 id)
{
    Function *f = m_doc->function(id);
    if (f == nullptr)
        return;

    if (f->isRunning())
        f->stop(FunctionParent::master());
    else
        f->start(m_doc->masterTimer(), FunctionParent::master());
}

bool FunctionManager::effectIsRunning(quint32 id) const
{
    Function *f = m_doc->function(id);
    return f != nullptr && f->isRunning();
}

/*****************************************************************************
 * Moving Head visual editor
 *****************************************************************************/

QVariantList FunctionManager::movingEditorFixtures() const
{
    QVariantList list;

    for (Fixture *f : m_doc->fixtures())
    {
        if (f == nullptr)
            continue;

        bool hasPan = f->channel(QLCChannel::Pan) != QLCChannel::invalid();
        bool hasTilt = f->channel(QLCChannel::Tilt) != QLCChannel::invalid();
        if (!hasPan && !hasTilt)
            continue;

        QVariantMap map;
        map.insert("id", f->id());
        map.insert("name", f->name());
        map.insert("hasPan", hasPan);
        map.insert("hasTilt", hasTilt);
        list << map;
    }

    return list;
}

void FunctionManager::movingEditorPreview(QVariantList fixtureIds, qreal panFraction, qreal tiltFraction)
{
    if (m_movingEditorSource == nullptr)
        m_movingEditorSource = new GenericDMXSource(m_doc);

    m_movingEditorSource->setOutputEnabled(true);

    for (const QVariant &v : fixtureIds)
    {
        Fixture *f = m_doc->fixture(quint32(v.toUInt()));
        if (f == nullptr)
            continue;

        const QList<SceneValue> values = aiComputePositionValues(f, float(panFraction), float(tiltFraction));
        for (const SceneValue &sv : values)
            m_movingEditorSource->set(sv.fxi, sv.channel, sv.value);
    }
}

void FunctionManager::movingEditorClearPreview()
{
    if (m_movingEditorSource == nullptr)
        return;

    m_movingEditorSource->unsetAll();
    m_movingEditorSource->setOutputEnabled(false);
}

quint32 FunctionManager::movingEditorCreate(QString groupPath, QString name, QVariantList fixtureIds,
                                             QVariantList mirrorFixtureIds, QVariantList points,
                                             int deviceOffsetSteps)
{
    if (points.isEmpty())
        return Function::invalidId();

    QList<Fixture*> fixtures;
    for (const QVariant &v : fixtureIds)
    {
        Fixture *f = m_doc->fixture(quint32(v.toUInt()));
        if (f != nullptr)
            fixtures << f;
    }

    QList<Fixture*> mirrorFixtures;
    for (const QVariant &v : mirrorFixtureIds)
    {
        Fixture *f = m_doc->fixture(quint32(v.toUInt()));
        if (f != nullptr)
            mirrorFixtures << f;
    }

    if (fixtures.isEmpty() && mirrorFixtures.isEmpty())
        return Function::invalidId();

    if (name.trimmed().isEmpty())
        name = tr("Moving Effekt");

    // Parsed once, up front - every step below (see the "wave" branch of
    // the loop) needs random access into the full list, not just the
    // current point.
    struct Keyframe { float pan; float tilt; uint holdMs; uint fadeMs; };
    QList<Keyframe> path;
    for (const QVariant &pv : points)
    {
        QVariantMap p = pv.toMap();
        path << Keyframe{
            float(p.value("pan", 0.5).toDouble()),
            float(p.value("tilt", 0.5).toDouble()),
            uint(qMax(0, p.value("holdMs", 500).toInt())),
            uint(qMax(0, p.value("fadeMs", 500).toInt()))
        };
    }
    const int pointCount = path.count();
    deviceOffsetSteps = qMax(0, deviceOffsetSteps);

    QList<quint32> stepIds;
    QList<uint> holds, fades;

    for (int s = 0; s < pointCount; s++)
    {
        const Keyframe &stepKeyframe = path.at(s); // step's own timing, regardless of stagger
        Scene *scene = aiCreateScene(name);
        if (scene == nullptr)
            return Function::invalidId();

        for (int fi = 0; fi < fixtures.count(); fi++)
        {
            // deviceOffsetSteps == 0 collapses this to path.at(s) for
            // every fixture - identical to the pre-stagger behaviour.
            int idx = ((s - fi * deviceOffsetSteps) % pointCount + pointCount) % pointCount;
            const Keyframe &kf = path.at(idx);
            Fixture *f = fixtures.at(fi);
            scene->addFixture(f->id());
            aiSetFixturePosition(scene, f, kf.pan, kf.tilt);
        }
        // mirrored fixtures get the same tilt, but a horizontally flipped
        // pan - for a symmetric pair of moving heads that should move
        // "outward"/"inward" together rather than identically - staggered
        // the same way, counted separately from the regular fixture list.
        for (int fi = 0; fi < mirrorFixtures.count(); fi++)
        {
            int idx = ((s - fi * deviceOffsetSteps) % pointCount + pointCount) % pointCount;
            const Keyframe &kf = path.at(idx);
            Fixture *f = mirrorFixtures.at(fi);
            scene->addFixture(f->id());
            aiSetFixturePosition(scene, f, 1.0f - kf.pan, kf.tilt);
        }

        stepIds << scene->id();
        holds << stepKeyframe.holdMs;
        fades << stepKeyframe.fadeMs;
    }

    Chaser *chaser = new Chaser(m_doc);
    chaser->setFadeInMode(Chaser::PerStep);
    chaser->setFadeOutMode(Chaser::PerStep);
    chaser->setDurationMode(Chaser::PerStep);
    for (int i = 0; i < stepIds.count(); i++)
    {
        ChaserStep step(stepIds.at(i), fades.at(i), holds.at(i), fades.at(i));
        chaser->addStep(step);
    }

    quint32 cid = addFunctiontoDoc(chaser, name, false);
    if (cid == Function::invalidId())
        return Function::invalidId();

    m_chaserCount++;
    emit chaserCountChanged();

    groupPath = groupPath.trimmed();
    if (!groupPath.isEmpty())
        moveFunction(cid, groupPath);

    return cid;
}
