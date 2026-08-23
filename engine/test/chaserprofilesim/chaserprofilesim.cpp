/*
  Q Light Controller Plus
  chaserprofilesim.cpp

  Throwaway diagnostic tool (not part of the normal test suite / not
  registered in test.sh): reproduces exactly what
  VCMusicReactive::startOrStepChaser()/advanceCurrentProfileChasers() do
  to two simultaneously-running Chasers - same FunctionParent source,
  same setAction()/start() call sequence, driven from an external
  "beat" loop against a REAL, actually-running MasterTimer (its own
  QThread, not a stub) - using the real Doc/Function/Chaser/ChaserRunner
  production classes, not mocks. Built specifically because two rounds
  of live-testing via the actual qmlui widget with real audio input
  showed the profile's Chasers not advancing on Beat/Kick at all, and
  two different fixes to VCMusicReactive's own step-advance call made
  no observable difference - meaning the bug has to be reproducible (or
  provably NOT reproducible, which is just as useful) independent of
  audio capture, QML, or the qmlui widget entirely. Whatever this prints
  is ground truth from the real engine, not a live screenshot read
  several seconds apart.

  Usage: chaserprofilesim [beats=15] [beatMs=500] [stepDurationMs=5000]

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
#include <QDir>
#include <QThread>
#include <cstdio>

#include "chaseraction.h"
#include "chaserstep.h"
#include "chaser.h"
#include "doc.h"
#include "fixture.h"
#include "functionparent.h"
#include "mastertimer.h"
#include "qlcfile.h"
#include "qlcfixturedef.h"
#include "qlcfixturemode.h"
#include "scene.h"

// Not resource_paths.h's INTERNAL_FIXTUREDIR - that's relative to
// whatever CWD ctest happens to invoke tests from, which this
// throwaway tool isn't wired into. Absolute path to the real repo
// checkout instead, so it runs correctly from anywhere.
#define FIXTURE_DIR "/var/home/devbaka/Dokumente/GitHub/qlcplus/resources/fixtures/"

static Chaser *buildChaser(Doc *doc, Fixture *fxi, const QString &name,
                            int step0Val, int step1Val, uint stepDurationMs)
{
    Scene *s0 = new Scene(doc);
    s0->setName(name + " step0");
    for (quint32 i = 0; i < fxi->channels(); i++)
        s0->setValue(fxi->id(), i, uchar(step0Val));
    s0->setDuration(stepDurationMs);
    doc->addFunction(s0);

    Scene *s1 = new Scene(doc);
    s1->setName(name + " step1");
    for (quint32 i = 0; i < fxi->channels(); i++)
        s1->setValue(fxi->id(), i, uchar(step1Val));
    s1->setDuration(stepDurationMs);
    doc->addFunction(s1);

    Chaser *ch = new Chaser(doc);
    ch->setName(name);
    ch->addStep(ChaserStep(s0->id()));
    ch->addStep(ChaserStep(s1->id()));
    doc->addFunction(ch);
    return ch;
}

// Byte-for-byte the same logic as
// VCMusicReactive::startOrStepChaser() in
// qmlui/virtualconsole/vcmusicreactive.cpp as of this build - keep in
// sync by hand if that function changes, same convention as wavanalyze.
static void startOrStepChaser(Chaser *ch, const FunctionParent &parent, MasterTimer *timer, double masterIntensity)
{
    bool wasRunning = ch->isRunning();
    std::printf("  startOrStepChaser(%-8s): wasRunning=%d stepsCount=%d currentStepIndex=%d\n",
                qPrintable(ch->name()), wasRunning, ch->stepsCount(), ch->currentStepIndex());

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

    int beats = argc > 1 ? std::atoi(argv[1]) : 15;
    int beatMs = argc > 2 ? std::atoi(argv[2]) : 500;
    uint stepDurationMs = argc > 3 ? uint(std::atoi(argv[3])) : 5000;

    Doc doc(nullptr);
    QDir dir(FIXTURE_DIR);
    dir.setFilter(QDir::Files);
    dir.setNameFilters(QStringList() << QString("*%1").arg(KExtFixture));
    doc.fixtureDefCache()->loadMap(dir);

    QLCFixtureDef *def = doc.fixtureDefCache()->fixtureDef("Futurelight", "DJScan250");
    if (!def) { std::fprintf(stderr, "fixture def not found under " FIXTURE_DIR "\n"); return 1; }
    QLCFixtureMode *mode = def->mode("Mode 1");
    if (!mode) { std::fprintf(stderr, "fixture mode not found\n"); return 1; }

    Fixture *fxi = new Fixture(&doc);
    fxi->setFixtureDefinition(def, mode);
    fxi->setName("Test Fixture");
    fxi->setAddress(0);
    fxi->setUniverse(0);
    doc.addFixture(fxi);

    // Two Chasers, two steps each - mirrors the user's real "PARs" /
    // "moving heads" two-Chaser Profile setup.
    Chaser *chaserA = buildChaser(&doc, fxi, "ChaserA", 255, 0, stepDurationMs);
    Chaser *chaserB = buildChaser(&doc, fxi, "ChaserB", 64, 200, stepDurationMs);

    MasterTimer *timer = doc.masterTimer();
    timer->start();
    QThread::msleep(200); // let the MasterTimer thread actually spin up

    // Same FunctionParent source for both Chasers - exactly like one
    // VCMusicReactive widget instance driving every Chaser in the
    // active Profile via the same functionParent().
    FunctionParent parent(FunctionParent::AutoVCWidget, 999);

    std::printf("=== %d simulated beats, %dms apart, step duration %dms ===\n\n", beats, beatMs, stepDurationMs);

    for (int beat = 0; beat < beats; beat++)
    {
        std::printf("--- beat %d (t=%dms) ---\n", beat, beat * beatMs);
        startOrStepChaser(chaserA, parent, timer, 1.0);
        startOrStepChaser(chaserB, parent, timer, 1.0);

        QThread::msleep(uint(beatMs));

        std::printf("  after tick: A step=%d/%d running=%d | B step=%d/%d running=%d\n\n",
                    chaserA->currentStepIndex(), chaserA->stepsCount(), chaserA->isRunning(),
                    chaserB->currentStepIndex(), chaserB->stepsCount(), chaserB->isRunning());
    }

    chaserA->stop(parent);
    chaserB->stop(parent);
    QThread::msleep(200);
    timer->stop();

    return 0;
}
