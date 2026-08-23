/*
  Q Light Controller Plus
  audioevents.h

  Musical event/state data model produced by the audio analysis layer
  (BeatOnsetExtractor / PercussionEventDetector / BeatTracker) and
  consumed by AudioCapture's Qt signals. Deliberately plain C++, no Qt
  and no knowledge of DMX/Functions/widgets: the analyzer only ever
  produces timestamped events and continuous values, it never decides
  what a Kick or a Break *does* on stage.

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

#ifndef AUDIOEVENTS_H
#define AUDIOEVENTS_H

/** @addtogroup engine_audio Audio
 * @{
 */

/** Discrete musical event types produced by the analysis layer. */
enum class AudioEventType
{
    Kick = 0,
    Snare,   // also used for claps: same mid-band onset front end
    HiHat,
    Beat,
    Bar
};

/** One discrete, timestamped musical event. */
struct AudioEvent
{
    AudioEventType type;
    double timestampSec;  // audio-timeline seconds, not wall clock
    double confidence;    // 0..1, how far above the adaptive threshold
    double strength;      // 0..1, normalized onset magnitude
};

/** Continuous analysis state, updated once per processed audio block. */
struct AudioAnalysisState
{
    double bpm = 0.0;
    double beatPhase = 0.0;         // 0..1 within the current beat
    int beatIndex = -1;             // monotonic beat counter, -1 = unknown
    int barIndex = -1;              // beatIndex / beatsPerBar, -1 = unknown

    double energy = 0.0;            // overall onset-band energy
    double bassEnergy = 0.0;
    double midEnergy = 0.0;
    double highEnergy = 0.0;

    double kickConfidence = 0.0;    // decays after the last Kick event
    double beatConfidence = 0.0;    // tempo estimator's own comb score

    double timeSinceLastKick = -1.0;
    double timeSinceLastBeat = -1.0;

    /** Diagnostics only: PercussionEventDetector's shared onset gate,
     *  as of the last hop - the raw peak value and the adaptive
     *  threshold it needs to exceed to register a Kick/Snare/HiHat.
     *  Lets a caller show how close real audio is to firing at all. */
    double peakBandValue = 0.0;
    double peakThreshold = 0.0;

    /** Kick band (<200Hz) ONLY rising-edge onset value, max over the
     *  block just processed - unlike peakBandValue (shared max across
     *  Kick+Snare+HiHat, used for classification) this stays low through
     *  a build-up section that still has hi-hats/percussion but no kick
     *  drum. Bounded to [0, 2/3], same as peakBandValue - see
     *  BeatOnsetExtractor::processHop(). */
    double kickBandOnset = 0.0;

    /** True once energy and kick activity have both stayed low for a
     *  sustained period (e.g. a vocal break with no kick drum). Derived
     *  purely from the above values - the analyzer does not know or
     *  care what a caller does with this. */
    bool inBreak = false;
};

/** @} */

#endif // AUDIOEVENTS_H
