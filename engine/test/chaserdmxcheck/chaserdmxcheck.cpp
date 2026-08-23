/*
  Q Light Controller Plus
  chaserdmxcheck.cpp

  Throwaway diagnostic tool (not part of the normal test suite / not
  registered in test.sh): loads a REAL project file (fixtures + functions,
  via the same Doc::loadXML() path the real app uses) and drives the
  named Chasers exactly like VCMusicReactive::startOrStepChaser() does -
  but instead of stopping at "setAction() was called", it reads back the
  actual post-Grandmaster Universe DMX byte for a given channel after
  each simulated beat. That is the literal value that would go out over
  DMX/Art-Net/whatever output is patched - the closest thing to "does the
  light actually change" checkable without physical hardware in front of
  a screen.

  Built specifically because a live audio-driven trace already proved
  ChaserRunner's step index advances correctly (see chaserprofilesim /
  the qDebug trace in vcmusicreactive.cpp), but there was no direct
  proof yet that a step change actually reaches the Universe's output
  buffer for a specific real fixture channel, as opposed to just an
  internal step counter.

  Usage: chaserdmxcheck <file.qxw> <chaserFunctionId> <universe> <channel> [beats=10] [beatMs=400]
    e.g. chaserdmxcheck qlchome.qxw 1  0 100   -> "Chaser - Wechsel dimmer", fixture 6 ch0
         chaserdmxcheck qlchome.qxw 10 0 250   -> "Chaser - MH Blink", fixture 36 ch5

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

#include <QCoreApplication>
#include <QFile>
#include <QThread>
#include <QXmlStreamReader>
#include <cstdio>

#include <QElapsedTimer>
#include <QEventLoop>

#include "chaser.h"
#include "chaseraction.h"
#include "chaserstep.h"
#include "doc.h"
#include "fixture.h"
#include "functionparent.h"
#include "inputoutputmap.h"
#include "mastertimer.h"
#include "qlcfile.h"
#include "universe.h"

// Byte-for-byte the same logic as VCMusicReactive::startOrStepChaser()
// as of this build - keep in sync by hand, same convention as the other
// throwaway diagnostics here.
// A plain QThread::msleep() here is not enough: Universe::tick() is
// only ever invoked via a Qt::QueuedConnection from
// MasterTimer::tickReady() (see InputOutputMap::addUniverse()), and
// Universe::run() waits on a semaphore that ONLY that queued slot
// delivery releases - so without an actual Qt event loop pumping on
// this (the connecting/main) thread, that delivery never happens,
// Universe::processFaders() never runs, and every channel reads back
// as 0 forever even though ChaserRunner's own step index (driven
// synchronously, not via a queued signal) advances completely
// correctly. Confirmed the hard way: first version of this tool used
// QThread::msleep() and read 0 on every single beat despite the step
// index alternating exactly as expected.
static void pump(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

static void startOrStepChaser(Chaser *ch, const FunctionParent &parent, MasterTimer *timer, double masterIntensity)
{
    bool wasRunning = ch->isRunning();

    ChaserAction action;
    action.m_masterIntensity = masterIntensity;
    action.m_stepIntensity = 1.0;
    action.m_fadeMode = 0;

    if (wasRunning)
    {
        action.m_action = ChaserNextStep;
        ch->setAction(action);
    }
    else
    {
        action.m_action = ChaserSetStepIndex;
        action.m_stepIndex = 0;
        ch->setAction(action);
        ch->start(timer, parent);
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    if (argc < 5)
    {
        std::fprintf(stderr, "usage: %s <file.qxw> <chaserFunctionId> <universe> <channel> [beats=10] [beatMs=400]\n", argv[0]);
        return 1;
    }

    QString filePath = argv[1];
    quint32 chaserId = QString(argv[2]).toUInt();
    int universeIdx = QString(argv[3]).toInt();
    int channel = QString(argv[4]).toInt();
    int beats = argc > 5 ? std::atoi(argv[5]) : 10;
    int beatMs = argc > 6 ? std::atoi(argv[6]) : 400;

    // Optional 2nd Chaser, driven every beat right alongside the first -
    // matches VCMusicReactive::advanceCurrentProfileChasers() looping
    // over every Chaser in a Profile, rather than testing each Chaser
    // in isolation.
    bool haveSecond = argc > 9;
    quint32 chaserId2 = haveSecond ? QString(argv[7]).toUInt() : 0;
    int universeIdx2 = haveSecond ? QString(argv[8]).toInt() : 0;
    int channel2 = haveSecond ? QString(argv[9]).toInt() : 0;

    Doc doc(nullptr);

    QXmlStreamReader *reader = QLCFile::getXMLReader(filePath);
    if (reader == nullptr || reader->device() == nullptr || reader->hasError())
    {
        std::fprintf(stderr, "cannot open %s\n", qPrintable(filePath));
        return 1;
    }
    while (!reader->atEnd())
    {
        if (reader->readNext() == QXmlStreamReader::DTD)
            break;
    }
    if (reader->dtdName() != QStringLiteral("Workspace"))
    {
        std::fprintf(stderr, "%s is not a QLC+ workspace file\n", qPrintable(filePath));
        return 1;
    }
    doc.setWorkspacePath(QFileInfo(filePath).absolutePath());

    if (!reader->readNextStartElement() || reader->name() != QStringLiteral("Workspace"))
    {
        std::fprintf(stderr, "Workspace node not found\n");
        return 1;
    }
    bool engineLoaded = false;
    while (reader->readNextStartElement())
    {
        if (reader->name() == QStringLiteral("Engine"))
        {
            // loadIO=true after all: a first attempt with false left
            // channel values at 0 even though the step index advanced
            // correctly - suspect fixture/universe channel patching is
            // gated on the same flag as physical output patching, not
            // just hardware. No real DMX interface exists in this
            // sandboxed test regardless, so an attempt to open one just
            // fails harmlessly (same as every qlcplus-qml GUI launch
            // used throughout this session).
            doc.loadXML(*reader, true);
            engineLoaded = true;
        }
        else
        {
            reader->skipCurrentElement();
        }
    }
    QLCFile::releaseXMLReader(reader);

    if (!engineLoaded)
    {
        std::fprintf(stderr, "no <Engine> section found - fixtures/functions not loaded\n");
        return 1;
    }

    // Universe is its own QThread (see universe.h) - its run() loop is
    // what actually calls processFaders() and writes ramped/faded values
    // into postGMValues() over time. Doc::addFixture() only starts it
    // when a NEW universe index has to be created to fit the fixture;
    // the default universes Doc's constructor already creates are left
    // unstarted until the app's own startup code calls this explicitly
    // (see app.cpp) - a standalone tool has to do the same, or every
    // channel read back as 0 forever regardless of what the Chaser/
    // GenericFader chain is doing (confirmed the hard way: first run of
    // this tool read 0 on every beat despite stepIndex advancing
    // correctly - this was the missing piece, not a Chaser bug).
    doc.inputOutputMap()->startUniverses();

    Function *func = doc.function(chaserId);
    Chaser *ch = qobject_cast<Chaser*>(func);
    if (ch == nullptr)
    {
        std::fprintf(stderr, "function id %u is not a Chaser (found %p, name '%s')\n",
                      chaserId, (void*)func, func ? qPrintable(func->name()) : "?");
        return 1;
    }

    std::printf("Chaser has %d steps:\n", ch->stepsCount());
    for (int i = 0; i < ch->stepsCount(); i++)
    {
        ChaserStep step = ch->steps().at(i);
        Function *stepFunc = doc.function(step.fid);
        std::printf("  step %d: fid=%u -> %s (name '%s')\n", i, step.fid,
                     stepFunc ? "FOUND" : "NULL!!", stepFunc ? qPrintable(stepFunc->name()) : "?");
    }
    std::printf("doc.fixtures().count() = %d\n", int(doc.fixtures().count()));
    Fixture *fxi = doc.fixture(6);
    std::printf("fixture id 6 -> %s (address %u, universe %u)\n",
                 fxi ? "FOUND" : "NULL!!", fxi ? fxi->address() : 0, fxi ? fxi->universe() : 0);

    QList<Universe*> universes = doc.inputOutputMap()->universes();
    if (universeIdx < 0 || universeIdx >= universes.count())
    {
        std::fprintf(stderr, "universe %d out of range (have %d)\n", universeIdx, int(universes.count()));
        return 1;
    }
    Universe *universe = universes.at(universeIdx);

    Chaser *ch2 = nullptr;
    Universe *universe2 = nullptr;
    if (haveSecond)
    {
        ch2 = qobject_cast<Chaser*>(doc.function(chaserId2));
        if (ch2 == nullptr)
        {
            std::fprintf(stderr, "2nd function id %u is not a Chaser\n", chaserId2);
            return 1;
        }
        if (universeIdx2 < 0 || universeIdx2 >= universes.count())
        {
            std::fprintf(stderr, "2nd universe %d out of range\n", universeIdx2);
            return 1;
        }
        universe2 = universes.at(universeIdx2);
    }

    MasterTimer *timer = doc.masterTimer();
    timer->start();
    pump(200);

    // Same FunctionParent source for both Chasers - exactly like one
    // VCMusicReactive widget instance driving every Chaser in the
    // active Profile via the same functionParent().
    FunctionParent parent(FunctionParent::AutoVCWidget, 999);

    if (haveSecond)
        std::printf("=== Driving 2 Chasers together: '%s' (ch[%d,%d]) + '%s' (ch[%d,%d]), %d beats @ %dms ===\n\n",
                    qPrintable(ch->name()), universeIdx, channel,
                    qPrintable(ch2->name()), universeIdx2, channel2, beats, beatMs);
    else
        std::printf("=== Driving Chaser '%s' (id %u), watching universe %d channel %d, %d beats @ %dms ===\n\n",
                    qPrintable(ch->name()), chaserId, universeIdx, channel, beats, beatMs);

    for (int beat = 0; beat < beats; beat++)
    {
        startOrStepChaser(ch, parent, timer, 1.0);
        if (haveSecond)
            startOrStepChaser(ch2, parent, timer, 1.0);
        pump(beatMs);

        uchar value = 0;
        const QByteArray *postGM = universe->postGMValues();
        if (postGM != nullptr && channel >= 0 && channel < postGM->size())
            value = uchar(postGM->at(channel));

        if (haveSecond)
        {
            uchar value2 = 0;
            const QByteArray *postGM2 = universe2->postGMValues();
            if (postGM2 != nullptr && channel2 >= 0 && channel2 < postGM2->size())
                value2 = uchar(postGM2->at(channel2));

            std::printf("beat %2d: [%s] step=%d run=%d ch[%d,%d]=%3d   |   [%s] step=%d run=%d ch[%d,%d]=%3d\n",
                        beat, qPrintable(ch->name()), ch->currentStepIndex(), ch->isRunning(), universeIdx, channel, int(value),
                        qPrintable(ch2->name()), ch2->currentStepIndex(), ch2->isRunning(), universeIdx2, channel2, int(value2));
        }
        else
        {
            uchar preGM = universe->preGMValue(channel);
            Function *stepFunc = doc.function(ch->steps().at(qMax(0, ch->currentStepIndex())).fid);

            std::printf("beat %2d: stepIndex=%d running=%d stepFuncRunning=%d masterTimerRunningFns=%d  ->  universe[%d] ch[%d] preGM=%d postGM=%d\n",
                        beat, ch->currentStepIndex(), ch->isRunning(),
                        stepFunc ? stepFunc->isRunning() : -1,
                        timer->runningFunctions(),
                        universeIdx, channel, int(preGM), int(value));
        }
    }

    ch->stop(parent);
    if (ch2 != nullptr)
        ch2->stop(parent);
    pump(200);
    timer->stop();

    return 0;
}
