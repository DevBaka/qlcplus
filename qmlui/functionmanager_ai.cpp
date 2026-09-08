/*
  Q Light Controller Plus
  functionmanager_ai.cpp

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
 * AI Effect Generator: talks to a local Ollama server to turn a natural
 * language (or preset) request into real Scene/Chaser Functions.
 *
 * Design: rather than asking the LLM to emit raw per-channel DMX values for
 * every fixture and step (unreliable for anything but tiny rigs, and wildly
 * fixture-specific), the LLM only has to classify the user's request into one
 * of a small set of known "patterns" (solid_color, alternate_color,
 * chase_on_off, rainbow_chase, random_dots, strobe, mirror_movement) plus a
 * few semantic parameters (colour names, step/group counts, timings). Each
 * pattern is then built deterministically in C++ against the *real* fixture
 * capabilities (RGB/CMY channels, master intensity, pan/tilt + physical
 * range) and 2D designer layout, via the exact same Scene/Chaser creation
 * path a user action would use (see addFunctiontoDoc()). "custom_steps" is
 * kept as an escape hatch for requests that don't fit any known pattern, in
 * which case the LLM may specify explicit per-fixture semantic values
 * (colour name / intensity / pan-tilt as a 0.0-1.0 fraction of range) per
 * step, which are still resolved to real channels here, never taken as raw
 * DMX bytes from the model.
 */

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QVector3D>
#include <algorithm>
#include <QtMath>

#include "functionmanager.h"
#include "monitorproperties.h"
#include "qlcfixturemode.h"
#include "qlcphysical.h"
#include "qlcchannel.h"
#include "chaserstep.h"
#include "fixture.h"
#include "chaser.h"
#include "scene.h"
#include "doc.h"

/*****************************************************************************
 * Simple property accessors
 *****************************************************************************/

bool FunctionManager::aiBusy() const
{
    return m_aiBusy;
}

QString FunctionManager::aiStatus() const
{
    return m_aiStatus;
}

QStringList FunctionManager::aiAvailableModels() const
{
    return m_aiAvailableModels;
}

QString FunctionManager::aiSelectedModel() const
{
    return m_aiSelectedModel;
}

void FunctionManager::setAiSelectedModel(QString model)
{
    if (m_aiSelectedModel == model)
        return;

    m_aiSelectedModel = model;
    emit aiSelectedModelChanged();
}

QString FunctionManager::aiServerUrl() const
{
    return m_aiServerUrl;
}

void FunctionManager::setAiServerUrl(QString url)
{
    if (m_aiServerUrl == url)
        return;

    m_aiServerUrl = url;
    emit aiServerUrlChanged();
}

void FunctionManager::setAiBusy(bool busy)
{
    if (m_aiBusy == busy)
        return;

    m_aiBusy = busy;
    emit aiBusyChanged();
}

void FunctionManager::setAiStatus(QString status)
{
    if (m_aiStatus == status)
        return;

    m_aiStatus = status;
    emit aiStatusChanged();
}

QStringList FunctionManager::aiPresetPrompts() const
{
    return QStringList()
        << tr("Szene: alle Fixtures auf Farbe Rot")
        << tr("Szene: alle Fixtures Farbe auf Blau")
        << tr("Szene: alle Fixtures Farbe auf Grün")
        << tr("Szene: alle Fixtures Rot und Blau gemischt, jedes erste Rot und jedes zweite Blau")
        << tr("Chaser: erstelle einen Chaser mit 2 Szenen, in dem jedes zweite Gerät im Wechsel an- und ausgeschaltet wird")
        << tr("Chaser: erstelle einen Rainbow-Effekt")
        << tr("Chaser: erstelle einen Effekt mit zufälligen Punkten")
        << tr("Erstelle einen Strobe-Effekt")
        << tr("Erstelle einen Moving-Effekt: die linken beiden Moving Heads bewegen sich nach links und "
              "rechts und sind nach vorne oben gerichtet, die rechten beiden genauso, aber spiegelverkehrt");
}

/*****************************************************************************
 * Ollama networking
 *****************************************************************************/

void FunctionManager::aiRefreshModels()
{
    if (m_aiNetworkManager == nullptr)
        m_aiNetworkManager = new QNetworkAccessManager(this);

    QNetworkRequest req(QUrl(m_aiServerUrl + QStringLiteral("/api/tags")));
    QNetworkReply *reply = m_aiNetworkManager->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply]()
    {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError)
            return;

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject())
            return;

        QStringList models;
        const QJsonArray arr = doc.object().value("models").toArray();
        for (const QJsonValue &v : arr)
            models << v.toObject().value("name").toString();

        if (models != m_aiAvailableModels)
        {
            m_aiAvailableModels = models;
            emit aiAvailableModelsChanged();
        }

        if (m_aiSelectedModel.isEmpty() && !models.isEmpty())
        {
            m_aiSelectedModel = models.first();
            emit aiSelectedModelChanged();
        }
    });
}

void FunctionManager::aiGenerate(QString prompt)
{
    if (m_aiBusy)
        return;

    prompt = prompt.trimmed();
    if (prompt.isEmpty())
    {
        emit aiFailed(tr("Bitte einen Befehl eingeben."));
        return;
    }

    if (m_doc->fixtures().isEmpty())
    {
        emit aiFailed(tr("Es sind keine Fixtures im Projekt vorhanden."));
        return;
    }

    setAiBusy(true);
    setAiStatus(tr("Analysiere Fixtures und 2D-Anordnung..."));
    m_aiPendingUserPrompt = prompt;

    aiRequestPlan(prompt);
}

void FunctionManager::aiQuickSetup()
{
    if (m_aiBusy)
        return;

    if (m_doc->fixtures().isEmpty())
    {
        emit aiFailed(tr("Es sind keine Fixtures im Projekt vorhanden."));
        return;
    }

    setAiBusy(true);
    setAiStatus(tr("Erstelle Inspirations-Set..."));

    const QList<Fixture*> allFixtures = m_doc->fixtures();
    QStringList created;

    auto makePlan = [](const QString &name, const QJsonObject &params)
    {
        QJsonObject plan;
        plan.insert("name", name);
        plan.insert("params", params);
        return plan;
    };

    // 1. a handful of solid-colour scenes - the simplest possible starting point
    struct SolidPreset { const char *color; QString name; };
    const SolidPreset solidPresets[] = {
        { "red",   tr("Alle Rot") },
        { "blue",  tr("Alle Blau") },
        { "green", tr("Alle Grün") },
        { "white", tr("Alle Weiß") }
    };
    for (const SolidPreset &sp : solidPresets)
    {
        QJsonObject params;
        params.insert("color", QString::fromLatin1(sp.color));
        QString summary = aiBuildSolidColor(makePlan(sp.name, params), allFixtures);
        if (!summary.isEmpty())
            created << summary;
    }

    // 2. alternating colours across the fixture line
    {
        QJsonArray colors;
        colors.append(QStringLiteral("red"));
        colors.append(QStringLiteral("blue"));
        QJsonObject params;
        params.insert("colors", colors);
        QString summary = aiBuildAlternateColor(makePlan(tr("Rot Blau Wechsel"), params), allFixtures);
        if (!summary.isEmpty())
            created << summary;
    }

    // 3. every-other-fixture on/off chase
    {
        QJsonObject params;
        params.insert("groups", 2);
        params.insert("holdMs", 500);
        QString summary = aiBuildChaseOnOff(makePlan(tr("Ein Aus Chase"), params), allFixtures);
        if (!summary.isEmpty())
            created << summary;
    }

    // 4. rainbow chase
    {
        QJsonObject params;
        params.insert("steps", 8);
        params.insert("holdMs", 400);
        params.insert("fadeMs", 300);
        QString summary = aiBuildRainbowChase(makePlan(tr("Regenbogen"), params), allFixtures);
        if (!summary.isEmpty())
            created << summary;
    }

    // 5. random dots
    {
        QJsonObject params;
        params.insert("steps", 10);
        params.insert("holdMs", 200);
        params.insert("density", 0.35);
        QString summary = aiBuildRandomDots(makePlan(tr("Zufallspunkte"), params), allFixtures);
        if (!summary.isEmpty())
            created << summary;
    }

    // 6. strobe
    {
        QJsonObject params;
        params.insert("rateMs", 60);
        QString summary = aiBuildStrobe(makePlan(tr("Strobe"), params), allFixtures);
        if (!summary.isEmpty())
            created << summary;
    }

    // 7. mirrored moving-head sweep - only if the rig actually has a pan/tilt fixture
    bool hasMovement = false;
    for (Fixture *f : allFixtures)
    {
        if (f != nullptr && (f->channel(QLCChannel::Pan) != QLCChannel::invalid() ||
                              f->channel(QLCChannel::Tilt) != QLCChannel::invalid()))
        {
            hasMovement = true;
            break;
        }
    }
    if (hasMovement)
    {
        QJsonObject params;
        params.insert("steps", 8);
        params.insert("holdMs", 400);
        params.insert("fadeMs", 600);
        QString summary = aiBuildMirrorMovement(makePlan(tr("Moving Spiegel"), params), allFixtures);
        if (!summary.isEmpty())
            created << summary;
    }

    setAiBusy(false);

    if (created.isEmpty())
    {
        setAiStatus(tr("Fehler"));
        emit aiFailed(tr("Es konnte kein Inspirations-Set erstellt werden."));
    }
    else
    {
        setAiStatus(tr("Fertig"));
        emit aiFinished(tr("Inspirations-Set erstellt (%1 Funktionen):\n").arg(created.count()) + created.join("\n"));
    }
}

void FunctionManager::aiRequestPlan(const QString &userPrompt)
{
    if (m_aiNetworkManager == nullptr)
        m_aiNetworkManager = new QNetworkAccessManager(this);

    QJsonArray fixtureCtx = aiBuildFixtureContext();

    QString schema = QStringLiteral(
        "Du bist ein Lichtsteuerungs-Assistent fuer die Lichtsteuerungs-Software QLC+. "
        "Du bekommst eine Liste der vorhandenen Scheinwerfer (Fixtures) mit ihren Faehigkeiten "
        "(hasColor/hasIntensity/hasPan/hasTilt) und, wenn vorhanden, ihrer 2D-Position im Lichtplan "
        "(position2D: x waechst nach rechts, y nach unten), sowie einen Wunsch des Nutzers auf Deutsch "
        "oder Englisch.\n"
        "Antworte AUSSCHLIESSLICH mit einem einzigen JSON-Objekt (keine Erklaerung, kein Markdown, "
        "keine Codebloecke), das genau folgendem Schema entspricht:\n"
        "{\n"
        "  \"action\": \"scene\" oder \"chaser\",\n"
        "  \"name\": \"kurzer Name auf Deutsch\",\n"
        "  \"pattern\": eines von \"solid_color\", \"alternate_color\", \"chase_on_off\", \"rainbow_chase\", "
        "\"random_dots\", \"strobe\", \"mirror_movement\", \"custom_steps\",\n"
        "  \"params\": {\n"
        "    \"color\": \"red|green|blue|white|yellow|cyan|magenta|orange|purple|amber|uv|...\",\n"
        "    \"colors\": [\"red\", \"blue\"],\n"
        "    \"fixtureIds\": [1,2,3],\n"
        "    \"steps\": 8,\n"
        "    \"holdMs\": 500,\n"
        "    \"fadeMs\": 200,\n"
        "    \"groups\": 2,\n"
        "    \"rateMs\": 60,\n"
        "    \"density\": 0.35,\n"
        "    \"leftFixtureIds\": [1,2],\n"
        "    \"rightFixtureIds\": [3,4],\n"
        "    \"panAmplitude\": 0.3,\n"
        "    \"tiltFraction\": 0.25\n"
        "  },\n"
        "  \"customSteps\": [\n"
        "    {\"holdMs\":500,\"fadeMs\":200,\"fixtures\":[{\"id\":1,\"color\":\"red\",\"intensity\":255,"
        "\"pan\":0.5,\"tilt\":0.5}]}\n"
        "  ]\n"
        "}\n\n"
        "Fuelle in \"params\" nur die fuer das gewaehlte \"pattern\" sinnvollen Felder aus, lasse den Rest weg. "
        "\"params.fixtureIds\" nur setzen, wenn der Nutzer ausdruecklich nur einen Teil der Fixtures meint - "
        "sonst weglassen (dann werden automatisch alle Fixtures verwendet). "
        "\"pattern\":\"custom_steps\" nur verwenden, wenn keines der anderen Muster den Wunsch abdeckt; "
        "gib dann in \"customSteps\" jeden Schritt einzeln an, mit den betroffenen Fixture-IDs, Farbe "
        "(\"color\" als Name), Helligkeit (\"intensity\" 0-255) und/oder Position (\"pan\"/\"tilt\" als "
        "Bruchteil 0.0-1.0 des jeweiligen Bewegungsbereichs, 0.5 = Mitte).\n\n"
        "Beispiele: \"alle Fixtures rot\" -> solid_color, color:red. \"jedes erste rot, jedes zweite blau\" "
        "-> alternate_color, colors:[red,blue]. \"jedes zweite Geraet blinkt\" -> chase_on_off, groups:2. "
        "\"Regenbogeneffekt\" -> rainbow_chase. \"zufaellige Punkte\" -> random_dots. \"Strobe\" -> strobe. "
        "\"Moving-Effekt mit linken/rechten Moving Heads gespiegelt\" -> mirror_movement (leftFixtureIds/"
        "rightFixtureIds weglassen, wenn nicht explizit genannt - wird dann automatisch anhand der "
        "2D-Position gespalten).\n\n"
        "Vorhandene Fixtures:\n") + QString::fromUtf8(QJsonDocument(fixtureCtx).toJson(QJsonDocument::Compact));

    QJsonObject sysMsg;
    sysMsg.insert("role", "system");
    sysMsg.insert("content", schema);

    QJsonObject userMsg;
    userMsg.insert("role", "user");
    userMsg.insert("content", userPrompt);

    QJsonArray messages;
    messages.append(sysMsg);
    messages.append(userMsg);

    QJsonObject body;
    body.insert("model", m_aiSelectedModel.isEmpty() ? QStringLiteral("qwen2.5:7b-instruct-q4_K_M") : m_aiSelectedModel);
    body.insert("messages", messages);
    body.insert("format", "json");
    body.insert("stream", false);

    QNetworkRequest req(QUrl(m_aiServerUrl + QStringLiteral("/api/chat")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    setAiStatus(tr("Frage KI (%1)...").arg(m_aiSelectedModel.isEmpty() ? tr("Standardmodell") : m_aiSelectedModel));

    QNetworkReply *reply = m_aiNetworkManager->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));

    connect(reply, &QNetworkReply::finished, this, [this, reply]()
    {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError)
        {
            setAiBusy(false);
            setAiStatus(tr("Fehler"));
            emit aiFailed(tr("Verbindung zu Ollama fehlgeschlagen (%1). Laeuft Ollama unter %2?")
                          .arg(reply->errorString(), m_aiServerUrl));
            return;
        }

        QByteArray data = reply->readAll();
        QJsonDocument outerDoc = QJsonDocument::fromJson(data);
        if (!outerDoc.isObject())
        {
            setAiBusy(false);
            setAiStatus(tr("Fehler"));
            emit aiFailed(tr("Unerwartete Antwort von Ollama."));
            return;
        }

        QJsonObject outer = outerDoc.object();
        QString content = outer.value("message").toObject().value("content").toString();

        QJsonDocument planDoc = QJsonDocument::fromJson(content.toUtf8());
        if (!planDoc.isObject())
        {
            // some models wrap the JSON in prose or markdown fences despite
            // instructions not to - try to salvage the first {...} block
            int start = content.indexOf('{');
            int end = content.lastIndexOf('}');
            if (start >= 0 && end > start)
                planDoc = QJsonDocument::fromJson(content.mid(start, end - start + 1).toUtf8());
        }

        if (!planDoc.isObject())
        {
            setAiBusy(false);
            setAiStatus(tr("Fehler"));
            emit aiFailed(tr("Die Antwort der KI konnte nicht als JSON gelesen werden."));
            return;
        }

        setAiStatus(tr("Erstelle Funktion(en)..."));
        aiApplyPlan(planDoc.object());
    });
}

/*****************************************************************************
 * Fixture context
 *****************************************************************************/

QJsonArray FunctionManager::aiBuildFixtureContext() const
{
    QJsonArray arr;
    MonitorProperties *mp = m_doc->monitorProperties();

    for (Fixture *f : m_doc->fixtures())
    {
        if (f == nullptr)
            continue;

        QJsonObject obj;
        obj.insert("id", int(f->id()));
        obj.insert("name", f->name());

        bool hasColor = f->rgbChannels(0).size() >= 3 || f->cmyChannels(0).size() >= 3;
        bool hasIntensity = f->masterIntensityChannel() != QLCChannel::invalid();
        bool hasPan = f->channel(QLCChannel::Pan) != QLCChannel::invalid();
        bool hasTilt = f->channel(QLCChannel::Tilt) != QLCChannel::invalid();

        obj.insert("hasColor", hasColor);
        obj.insert("hasIntensity", hasIntensity);
        obj.insert("hasPan", hasPan);
        obj.insert("hasTilt", hasTilt);

        if (mp != nullptr && mp->containsFixture(f->id()))
        {
            QVector3D pos = mp->fixturePosition(f->id(), 0, 0);
            QJsonObject posObj;
            posObj.insert("x", double(pos.x()));
            posObj.insert("y", double(pos.y()));
            obj.insert("position2D", posObj);
        }

        arr.append(obj);
    }

    return arr;
}

QList<Fixture*> FunctionManager::aiResolveFixtures(const QJsonObject &params) const
{
    QList<Fixture*> result;

    if (params.value("fixtureIds").isArray())
    {
        const QJsonArray ids = params.value("fixtureIds").toArray();
        for (const QJsonValue &v : ids)
        {
            Fixture *f = m_doc->fixture(quint32(v.toInt(-1)));
            if (f != nullptr)
                result << f;
        }
        if (!result.isEmpty())
            return result;
    }

    return m_doc->fixtures();
}

void FunctionManager::aiSortFixturesSpatially(QList<Fixture*> &fixtures) const
{
    MonitorProperties *mp = m_doc->monitorProperties();
    if (mp == nullptr)
        return;

    std::stable_sort(fixtures.begin(), fixtures.end(),
        [mp](Fixture *a, Fixture *b)
        {
            bool haveA = a != nullptr && mp->containsFixture(a->id());
            bool haveB = b != nullptr && mp->containsFixture(b->id());
            if (!haveA || !haveB)
                return a->id() < b->id();
            return mp->fixturePosition(a->id(), 0, 0).x() < mp->fixturePosition(b->id(), 0, 0).x();
        });
}

/*****************************************************************************
 * Channel-level helpers
 *****************************************************************************/

void FunctionManager::aiSetFixtureColor(Scene *scene, Fixture *fixture, QColor color)
{
    if (scene == nullptr || fixture == nullptr)
        return;

    quint32 master = fixture->masterIntensityChannel();
    if (master != QLCChannel::invalid())
        scene->setValue(SceneValue(fixture->id(), master, 255));

    QVector<quint32> rgb = fixture->rgbChannels(0);
    if (rgb.size() >= 3)
    {
        scene->setValue(SceneValue(fixture->id(), rgb.at(0), uchar(color.red())));
        scene->setValue(SceneValue(fixture->id(), rgb.at(1), uchar(color.green())));
        scene->setValue(SceneValue(fixture->id(), rgb.at(2), uchar(color.blue())));
        return;
    }

    QVector<quint32> cmy = fixture->cmyChannels(0);
    if (cmy.size() >= 3)
    {
        scene->setValue(SceneValue(fixture->id(), cmy.at(0), uchar(255 - color.red())));
        scene->setValue(SceneValue(fixture->id(), cmy.at(1), uchar(255 - color.green())));
        scene->setValue(SceneValue(fixture->id(), cmy.at(2), uchar(255 - color.blue())));
        return;
    }

    // no colour mixing available on this fixture - fall back to just turning
    // it fully on, so the request still has a visible effect
    if (master == QLCChannel::invalid())
        aiSetFixtureIntensity(scene, fixture, 255);
}

void FunctionManager::aiSetFixtureIntensity(Scene *scene, Fixture *fixture, uchar value)
{
    if (scene == nullptr || fixture == nullptr)
        return;

    // set every channel that contributes to visible brightness, so a later
    // step in the same Chaser can't leave stale, uncontrolled values behind
    quint32 master = fixture->masterIntensityChannel();
    if (master != QLCChannel::invalid())
        scene->setValue(SceneValue(fixture->id(), master, value));

    QVector<quint32> rgb = fixture->rgbChannels(0);
    if (rgb.size() >= 3)
    {
        for (quint32 ch : rgb)
            scene->setValue(SceneValue(fixture->id(), ch, value));
        return;
    }

    QVector<quint32> cmy = fixture->cmyChannels(0);
    if (cmy.size() >= 3)
    {
        // CMY is subtractive: 0 = full colour/open, 255 = fully subtracted/closed
        uchar cmyVal = value > 0 ? 0 : 255;
        for (quint32 ch : cmy)
            scene->setValue(SceneValue(fixture->id(), ch, cmyVal));
        return;
    }

    // last resort: some fixtures (e.g. multi-pixel bars) have several plain
    // Intensity-group channels but no single aggregate "master" channel - hit
    // every one of them directly, so the fixture still visibly reacts
    if (master == QLCChannel::invalid())
    {
        const QSet<quint32> allIntensity = fixture->channels(QLCChannel::Intensity);
        for (quint32 ch : allIntensity)
            scene->setValue(SceneValue(fixture->id(), ch, value));
    }
}

QList<SceneValue> FunctionManager::aiComputePositionValues(Fixture *fixture, float panFraction, float tiltFraction)
{
    QList<SceneValue> result;
    if (fixture == nullptr || fixture->fixtureMode() == nullptr)
        return result;

    QLCPhysical phy = fixture->fixtureMode()->physical();

    if (fixture->channel(QLCChannel::Pan) != QLCChannel::invalid())
    {
        int maxDeg = phy.focusPanMax();
        if (maxDeg <= 0)
            maxDeg = 360;
        float degrees = qBound(0.0f, panFraction, 1.0f) * float(maxDeg);
        result << fixture->positionToValues(QLCChannel::Pan, degrees);
    }

    if (fixture->channel(QLCChannel::Tilt) != QLCChannel::invalid())
    {
        int maxDeg = phy.focusTiltMax();
        if (maxDeg <= 0)
            maxDeg = 270;
        float degrees = qBound(0.0f, tiltFraction, 1.0f) * float(maxDeg);
        result << fixture->positionToValues(QLCChannel::Tilt, degrees);
    }

    return result;
}

void FunctionManager::aiSetFixturePosition(Scene *scene, Fixture *fixture, float panFraction, float tiltFraction)
{
    if (scene == nullptr)
        return;

    const QList<SceneValue> values = aiComputePositionValues(fixture, panFraction, tiltFraction);
    for (const SceneValue &sv : values)
        scene->setValue(sv);
}

QColor FunctionManager::aiParseColor(const QString &nameIn)
{
    QString name = nameIn.trimmed().toLower();
    name.remove(' ');

    static const QMap<QString, QColor> table = {
        { "red", QColor(255, 0, 0) },           { "rot", QColor(255, 0, 0) },
        { "green", QColor(0, 255, 0) },         { "grün", QColor(0, 255, 0) },       { "gruen", QColor(0, 255, 0) },
        { "blue", QColor(0, 0, 255) },          { "blau", QColor(0, 0, 255) },
        { "white", QColor(255, 255, 255) },     { "weiß", QColor(255, 255, 255) },   { "weiss", QColor(255, 255, 255) },
        { "yellow", QColor(255, 255, 0) },      { "gelb", QColor(255, 255, 0) },
        { "cyan", QColor(0, 255, 255) },        { "türkis", QColor(0, 255, 255) },
        { "magenta", QColor(255, 0, 255) },
        { "pink", QColor(255, 20, 147) },       { "rosa", QColor(255, 20, 147) },
        { "orange", QColor(255, 140, 0) },
        { "amber", QColor(255, 126, 0) },
        { "purple", QColor(160, 32, 240) },     { "violett", QColor(160, 32, 240) }, { "lila", QColor(160, 32, 240) },
        { "uv", QColor(148, 0, 211) },          { "ultraviolet", QColor(148, 0, 211) },
        { "warmwhite", QColor(255, 214, 170) }, { "warmweiß", QColor(255, 214, 170) },
        { "black", QColor(0, 0, 0) },           { "schwarz", QColor(0, 0, 0) },
        { "off", QColor(0, 0, 0) },             { "aus", QColor(0, 0, 0) }
    };

    if (table.contains(name))
        return table.value(name);

    QColor c(nameIn.trimmed());
    if (c.isValid())
        return c;

    return QColor(255, 255, 255);
}

/*****************************************************************************
 * Function creation helpers
 *****************************************************************************/

Scene *FunctionManager::aiCreateScene(const QString &baseName)
{
    Scene *scene = new Scene(m_doc);
    quint32 id = addFunctiontoDoc(scene, baseName, false);
    if (id == Function::invalidId())
        return nullptr;

    m_sceneCount++;
    emit sceneCountChanged();

    return scene;
}

Chaser *FunctionManager::aiCreateChaser(const QString &baseName, const QList<quint32> &stepFunctionIds,
                                         uint fadeInMs, uint holdMs, uint fadeOutMs)
{
    Chaser *chaser = new Chaser(m_doc);
    chaser->setFadeInMode(Chaser::PerStep);
    chaser->setFadeOutMode(Chaser::PerStep);
    chaser->setDurationMode(Chaser::PerStep);

    for (quint32 fid : stepFunctionIds)
    {
        ChaserStep step(fid, fadeInMs, holdMs, fadeOutMs);
        chaser->addStep(step);
    }

    quint32 id = addFunctiontoDoc(chaser, baseName, false);
    if (id == Function::invalidId())
        return nullptr;

    m_chaserCount++;
    emit chaserCountChanged();

    return chaser;
}

/*****************************************************************************
 * Plan dispatch
 *****************************************************************************/

void FunctionManager::aiApplyPlan(const QJsonObject &plan)
{
    QString pattern = plan.value("pattern").toString();
    QJsonObject params = plan.value("params").toObject();
    QList<Fixture*> fixtures = aiResolveFixtures(params);

    if (fixtures.isEmpty())
    {
        setAiBusy(false);
        setAiStatus(tr("Fehler"));
        emit aiFailed(tr("Keine passenden Fixtures gefunden."));
        return;
    }

    QString summary;

    if (pattern == QStringLiteral("solid_color"))
        summary = aiBuildSolidColor(plan, fixtures);
    else if (pattern == QStringLiteral("alternate_color"))
        summary = aiBuildAlternateColor(plan, fixtures);
    else if (pattern == QStringLiteral("chase_on_off"))
        summary = aiBuildChaseOnOff(plan, fixtures);
    else if (pattern == QStringLiteral("rainbow_chase"))
        summary = aiBuildRainbowChase(plan, fixtures);
    else if (pattern == QStringLiteral("random_dots"))
        summary = aiBuildRandomDots(plan, fixtures);
    else if (pattern == QStringLiteral("strobe"))
        summary = aiBuildStrobe(plan, fixtures);
    else if (pattern == QStringLiteral("mirror_movement"))
        summary = aiBuildMirrorMovement(plan, fixtures);
    else if (pattern == QStringLiteral("custom_steps"))
        summary = aiBuildCustomSteps(plan, fixtures);
    else
    {
        setAiBusy(false);
        setAiStatus(tr("Fehler"));
        emit aiFailed(tr("Unbekanntes Muster von der KI erhalten: \"%1\"").arg(pattern));
        return;
    }

    setAiBusy(false);

    if (summary.isEmpty())
    {
        setAiStatus(tr("Fehler"));
        emit aiFailed(tr("Die Antwort der KI konnte nicht in eine Funktion umgesetzt werden."));
    }
    else
    {
        setAiStatus(tr("Fertig"));
        emit aiFinished(summary);
    }
}

/*****************************************************************************
 * Pattern builders
 *****************************************************************************/

QString FunctionManager::aiBuildSolidColor(const QJsonObject &plan, const QList<Fixture*> &fixtures)
{
    QJsonObject params = plan.value("params").toObject();
    QString colorName = params.value("color").toString();

    if (colorName.isEmpty())
    {
        const QJsonArray colors = params.value("colors").toArray();
        if (!colors.isEmpty())
            colorName = colors.first().toString();
    }
    if (colorName.isEmpty())
        colorName = QStringLiteral("white");

    QColor color = aiParseColor(colorName);

    QString name = plan.value("name").toString();
    if (name.isEmpty())
        name = tr("KI Szene");

    Scene *scene = aiCreateScene(name);
    if (scene == nullptr)
        return QString();

    for (Fixture *f : fixtures)
    {
        scene->addFixture(f->id());
        aiSetFixtureColor(scene, f, color);
    }

    return tr("Szene \"%1\" erstellt (%2 Fixtures, Farbe %3)").arg(scene->name()).arg(fixtures.count()).arg(colorName);
}

QString FunctionManager::aiBuildAlternateColor(const QJsonObject &plan, const QList<Fixture*> &fixturesIn)
{
    QJsonObject params = plan.value("params").toObject();
    QStringList colorNames;
    for (const QJsonValue &v : params.value("colors").toArray())
        colorNames << v.toString();
    if (colorNames.isEmpty())
        colorNames << QStringLiteral("red") << QStringLiteral("blue");

    QList<Fixture*> fixtures = fixturesIn;
    aiSortFixturesSpatially(fixtures);

    QString name = plan.value("name").toString();
    if (name.isEmpty())
        name = tr("KI Szene");

    Scene *scene = aiCreateScene(name);
    if (scene == nullptr)
        return QString();

    for (int i = 0; i < fixtures.count(); i++)
    {
        Fixture *f = fixtures.at(i);
        QColor c = aiParseColor(colorNames.at(i % colorNames.count()));
        scene->addFixture(f->id());
        aiSetFixtureColor(scene, f, c);
    }

    return tr("Szene \"%1\" erstellt (%2 Fixtures, Farben: %3)")
            .arg(scene->name()).arg(fixtures.count()).arg(colorNames.join(", "));
}

QString FunctionManager::aiBuildChaseOnOff(const QJsonObject &plan, const QList<Fixture*> &fixturesIn)
{
    QJsonObject params = plan.value("params").toObject();
    int groups = qBound(2, params.value("groups").toInt(2), 8);
    uint holdMs = uint(params.value("holdMs").toInt(500));
    uint fadeMs = uint(params.value("fadeMs").toInt(0));

    QList<Fixture*> fixtures = fixturesIn;
    aiSortFixturesSpatially(fixtures);

    QString baseName = plan.value("name").toString();
    if (baseName.isEmpty())
        baseName = tr("KI Chase");

    QList<quint32> stepIds;
    for (int g = 0; g < groups; g++)
    {
        Scene *scene = aiCreateScene(baseName + QStringLiteral(" ") + tr("Schritt") + QStringLiteral(" ") + QString::number(g + 1));
        if (scene == nullptr)
            return QString();

        for (int i = 0; i < fixtures.count(); i++)
        {
            Fixture *f = fixtures.at(i);
            scene->addFixture(f->id());
            if ((i % groups) == g)
                aiSetFixtureColor(scene, f, QColor(255, 255, 255));
            else
                aiSetFixtureIntensity(scene, f, 0);
        }
        stepIds << scene->id();
    }

    Chaser *chaser = aiCreateChaser(baseName, stepIds, fadeMs, holdMs, fadeMs);
    if (chaser == nullptr)
        return QString();

    return tr("Chaser \"%1\" erstellt (%2 Schritte, %3 Fixtures)")
            .arg(chaser->name()).arg(groups).arg(fixtures.count());
}

QString FunctionManager::aiBuildRainbowChase(const QJsonObject &plan, const QList<Fixture*> &fixturesIn)
{
    QJsonObject params = plan.value("params").toObject();
    int steps = qBound(2, params.value("steps").toInt(8), 24);
    uint holdMs = uint(params.value("holdMs").toInt(400));
    uint fadeMs = uint(params.value("fadeMs").toInt(300));

    QList<Fixture*> fixtures = fixturesIn;
    aiSortFixturesSpatially(fixtures);

    QList<Fixture*> colorFixtures;
    for (Fixture *f : fixtures)
        if (f->rgbChannels(0).size() >= 3 || f->cmyChannels(0).size() >= 3)
            colorFixtures << f;
    if (colorFixtures.isEmpty())
        colorFixtures = fixtures;

    QString baseName = plan.value("name").toString();
    if (baseName.isEmpty())
        baseName = tr("KI Regenbogen");

    QList<quint32> stepIds;
    for (int s = 0; s < steps; s++)
    {
        Scene *scene = aiCreateScene(baseName + QStringLiteral(" ") + QString::number(s + 1));
        if (scene == nullptr)
            return QString();

        int baseHue = (s * 360) / steps;
        for (int i = 0; i < colorFixtures.count(); i++)
        {
            Fixture *f = colorFixtures.at(i);
            int hueOffset = colorFixtures.count() > 0 ? (i * 360) / colorFixtures.count() : 0;
            int hue = (baseHue + hueOffset) % 360;
            QColor c = QColor::fromHsv(hue, 255, 255);
            scene->addFixture(f->id());
            aiSetFixtureColor(scene, f, c);
        }
        stepIds << scene->id();
    }

    Chaser *chaser = aiCreateChaser(baseName, stepIds, fadeMs, holdMs, fadeMs);
    if (chaser == nullptr)
        return QString();

    return tr("Regenbogen-Chaser \"%1\" erstellt (%2 Schritte, %3 Fixtures)")
            .arg(chaser->name()).arg(steps).arg(colorFixtures.count());
}

QString FunctionManager::aiBuildRandomDots(const QJsonObject &plan, const QList<Fixture*> &fixtures)
{
    QJsonObject params = plan.value("params").toObject();
    int steps = qBound(2, params.value("steps").toInt(10), 32);
    uint holdMs = uint(params.value("holdMs").toInt(200));
    uint fadeMs = uint(params.value("fadeMs").toInt(50));
    double density = qBound(0.05, params.value("density").toDouble(0.35), 1.0);

    QString baseName = plan.value("name").toString();
    if (baseName.isEmpty())
        baseName = tr("KI Random");

    static const QColor palette[] = {
        QColor(255, 0, 0), QColor(0, 255, 0), QColor(0, 0, 255), QColor(255, 255, 0),
        QColor(0, 255, 255), QColor(255, 0, 255), QColor(255, 255, 255), QColor(255, 140, 0)
    };
    const int paletteSize = int(sizeof(palette) / sizeof(palette[0]));

    QList<quint32> stepIds;
    for (int s = 0; s < steps; s++)
    {
        Scene *scene = aiCreateScene(baseName + QStringLiteral(" ") + QString::number(s + 1));
        if (scene == nullptr)
            return QString();

        for (Fixture *f : fixtures)
        {
            scene->addFixture(f->id());
            if (QRandomGenerator::global()->generateDouble() < density)
            {
                QColor c = palette[QRandomGenerator::global()->bounded(paletteSize)];
                aiSetFixtureColor(scene, f, c);
            }
            else
            {
                aiSetFixtureIntensity(scene, f, 0);
            }
        }
        stepIds << scene->id();
    }

    Chaser *chaser = aiCreateChaser(baseName, stepIds, fadeMs, holdMs, fadeMs);
    if (chaser == nullptr)
        return QString();

    return tr("Random-Chaser \"%1\" erstellt (%2 Schritte, %3 Fixtures)")
            .arg(chaser->name()).arg(steps).arg(fixtures.count());
}

QString FunctionManager::aiBuildStrobe(const QJsonObject &plan, const QList<Fixture*> &fixtures)
{
    QJsonObject params = plan.value("params").toObject();
    uint rateMs = qMax(20, params.value("rateMs").toInt(60));
    QString colorName = params.value("color").toString();
    QColor color = colorName.isEmpty() ? QColor(255, 255, 255) : aiParseColor(colorName);

    QString baseName = plan.value("name").toString();
    if (baseName.isEmpty())
        baseName = tr("KI Strobe");

    Scene *onScene = aiCreateScene(baseName + QStringLiteral(" ") + tr("An"));
    if (onScene == nullptr)
        return QString();
    Scene *offScene = aiCreateScene(baseName + QStringLiteral(" ") + tr("Aus"));
    if (offScene == nullptr)
        return QString();

    for (Fixture *f : fixtures)
    {
        onScene->addFixture(f->id());
        aiSetFixtureColor(onScene, f, color);
        offScene->addFixture(f->id());
        aiSetFixtureIntensity(offScene, f, 0);
    }

    QList<quint32> stepIds;
    stepIds << onScene->id() << offScene->id();

    Chaser *chaser = aiCreateChaser(baseName, stepIds, 0, rateMs, 0);
    if (chaser == nullptr)
        return QString();

    return tr("Strobe-Chaser \"%1\" erstellt (%2 Fixtures, %3 ms)")
            .arg(chaser->name()).arg(fixtures.count()).arg(rateMs);
}

QString FunctionManager::aiBuildMirrorMovement(const QJsonObject &plan, const QList<Fixture*> &fixturesIn)
{
    QJsonObject params = plan.value("params").toObject();

    QList<Fixture*> leftFixtures, rightFixtures;

    // only accept fixtures that can actually move - guards against the LLM
    // naming a fixture by a "moving head"-sounding name that turns out to have
    // no real Pan/Tilt channel in its definition
    auto collectByIds = [this](const QJsonValue &arr, QList<Fixture*> &out)
    {
        for (const QJsonValue &v : arr.toArray())
        {
            Fixture *f = m_doc->fixture(quint32(v.toInt(-1)));
            if (f != nullptr && (f->channel(QLCChannel::Pan) != QLCChannel::invalid() ||
                                  f->channel(QLCChannel::Tilt) != QLCChannel::invalid()))
                out << f;
        }
    };

    if (params.value("leftFixtureIds").isArray())
        collectByIds(params.value("leftFixtureIds"), leftFixtures);
    if (params.value("rightFixtureIds").isArray())
        collectByIds(params.value("rightFixtureIds"), rightFixtures);

    // only movement-capable fixtures matter here
    QList<Fixture*> moveFixtures;
    for (Fixture *f : fixturesIn)
        if (f->channel(QLCChannel::Pan) != QLCChannel::invalid() || f->channel(QLCChannel::Tilt) != QLCChannel::invalid())
            moveFixtures << f;

    if (leftFixtures.isEmpty() && rightFixtures.isEmpty())
    {
        // auto-split by 2D x position (left half / right half), when known
        MonitorProperties *mp = m_doc->monitorProperties();
        QList<QPair<Fixture*, float>> withX;
        bool havePositions = mp != nullptr && !moveFixtures.isEmpty();

        if (havePositions)
        {
            for (Fixture *f : moveFixtures)
            {
                if (!mp->containsFixture(f->id()))
                {
                    havePositions = false;
                    break;
                }
                withX << qMakePair(f, mp->fixturePosition(f->id(), 0, 0).x());
            }
        }

        if (havePositions)
        {
            std::sort(withX.begin(), withX.end(),
                      [](const QPair<Fixture*, float> &a, const QPair<Fixture*, float> &b) { return a.second < b.second; });
            int leftCount = withX.count() / 2;
            for (int i = 0; i < withX.count(); i++)
            {
                if (i < leftCount)
                    leftFixtures << withX.at(i).first;
                else
                    rightFixtures << withX.at(i).first;
            }
        }
        else
        {
            // no 2D layout known for all fixtures - fall back to id order
            int leftCount = moveFixtures.count() / 2;
            for (int i = 0; i < moveFixtures.count(); i++)
            {
                if (i < leftCount)
                    leftFixtures << moveFixtures.at(i);
                else
                    rightFixtures << moveFixtures.at(i);
            }
        }
    }

    if (leftFixtures.isEmpty() && rightFixtures.isEmpty())
        return QString();

    int steps = qBound(4, params.value("steps").toInt(8), 24);
    uint holdMs = uint(params.value("holdMs").toInt(400));
    uint fadeMs = uint(params.value("fadeMs").toInt(600));
    double amplitude = qBound(0.05, params.value("panAmplitude").toDouble(0.3), 0.5);
    double tiltFraction = qBound(0.0, params.value("tiltFraction").toDouble(0.25), 1.0);

    QString baseName = plan.value("name").toString();
    if (baseName.isEmpty())
        baseName = tr("KI Movement");

    QList<quint32> stepIds;
    for (int s = 0; s < steps; s++)
    {
        // triangle-ish wave via sine, so the movement sweeps smoothly back and forth
        double phase = (double(s) / double(steps)) * 2.0 * M_PI;
        double offset = std::sin(phase); // -1..1

        Scene *scene = aiCreateScene(baseName + QStringLiteral(" ") + QString::number(s + 1));
        if (scene == nullptr)
            return QString();

        for (Fixture *f : leftFixtures)
        {
            scene->addFixture(f->id());
            aiSetFixturePosition(scene, f, float(0.5 + offset * amplitude), float(tiltFraction));
        }
        for (Fixture *f : rightFixtures)
        {
            scene->addFixture(f->id());
            // mirrored: the right group swings the opposite way of the left group
            aiSetFixturePosition(scene, f, float(0.5 - offset * amplitude), float(tiltFraction));
        }
        stepIds << scene->id();
    }

    Chaser *chaser = aiCreateChaser(baseName, stepIds, fadeMs, holdMs, fadeMs);
    if (chaser == nullptr)
        return QString();

    return tr("Movement-Chaser \"%1\" erstellt (%2 links / %3 rechts, %4 Schritte)")
            .arg(chaser->name()).arg(leftFixtures.count()).arg(rightFixtures.count()).arg(steps);
}

QString FunctionManager::aiBuildCustomSteps(const QJsonObject &plan, const QList<Fixture*> &fixtures)
{
    Q_UNUSED(fixtures)

    QJsonArray steps = plan.value("customSteps").toArray();
    if (steps.isEmpty())
        return QString();

    QString baseName = plan.value("name").toString();
    if (baseName.isEmpty())
        baseName = tr("KI Effekt");

    QList<quint32> stepIds;
    QList<uint> holds, fades;

    for (int s = 0; s < steps.count(); s++)
    {
        QJsonObject stepObj = steps.at(s).toObject();
        uint holdMs = uint(stepObj.value("holdMs").toInt(500));
        uint fadeMs = uint(stepObj.value("fadeMs").toInt(200));

        Scene *scene = aiCreateScene(baseName + QStringLiteral(" ") + QString::number(s + 1));
        if (scene == nullptr)
            return QString();

        for (const QJsonValue &fv : stepObj.value("fixtures").toArray())
        {
            QJsonObject fxObj = fv.toObject();
            Fixture *f = m_doc->fixture(quint32(fxObj.value("id").toInt(-1)));
            if (f == nullptr)
                continue;

            scene->addFixture(f->id());

            if (fxObj.contains("color"))
                aiSetFixtureColor(scene, f, aiParseColor(fxObj.value("color").toString()));
            if (fxObj.contains("intensity"))
                aiSetFixtureIntensity(scene, f, uchar(qBound(0, fxObj.value("intensity").toInt(), 255)));
            if (fxObj.contains("pan") || fxObj.contains("tilt"))
                aiSetFixturePosition(scene, f, float(fxObj.value("pan").toDouble(0.5)), float(fxObj.value("tilt").toDouble(0.5)));
        }

        stepIds << scene->id();
        holds << holdMs;
        fades << fadeMs;
    }

    if (stepIds.count() == 1)
        return tr("Szene \"%1\" erstellt").arg(baseName);

    Chaser *chaser = new Chaser(m_doc);
    chaser->setFadeInMode(Chaser::PerStep);
    chaser->setFadeOutMode(Chaser::PerStep);
    chaser->setDurationMode(Chaser::PerStep);
    for (int i = 0; i < stepIds.count(); i++)
    {
        ChaserStep step(stepIds.at(i), fades.at(i), holds.at(i), fades.at(i));
        chaser->addStep(step);
    }

    quint32 cid = addFunctiontoDoc(chaser, baseName, false);
    if (cid == Function::invalidId())
        return QString();

    m_chaserCount++;
    emit chaserCountChanged();

    return tr("Chaser \"%1\" erstellt (%2 Schritte)").arg(chaser->name()).arg(stepIds.count());
}
