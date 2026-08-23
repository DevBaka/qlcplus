/*
  Q Light Controller Plus
  percussioneventdetector.h

  Adaptive-threshold peak-picker on top of BeatOnsetExtractor's
  already-computed per-band onset stream (kick <200Hz / snare-vocals
  200-4000Hz / hats >=4kHz, saturated rising-edge - see beattracker.md).
  Converts that continuous per-band signal into discrete Kick/Snare/HiHat
  AudioEvents.

  Deliberately reuses the existing, already-validated onset front end
  instead of adding a second one: the per-band signal already separates
  a kick transient from a sustained bass note (saturation against the
  band's own recent peak + rising-edge-only), so this class only has to
  decide *when* a hop's onset is large enough to count as a hit, and
  *which* band actually dominates it.

  Important: BeatOnsetExtractor's per-band saturation is deliberately
  tuned so a kick and a snare "spike near-equally" (beattracker.md,
  stage 4) - that equalization is exactly what makes the *combined*
  signal track a backbeat's every-beat periodicity for tempo estimation,
  but it also means three independent per-band thresholds (one kick can
  cross the kick band's OWN threshold, one snare can cross the snare
  band's OWN threshold, but a snare's saturated kick-band response can
  ALSO cross the kick band's threshold at nearly the same instant)
  cannot cleanly tell instruments apart on their own - measured directly
  by percussionevents_test: independent per-band thresholds fired both
  Kick and Snare on essentially every hit, regardless of which one it
  actually was. The fix is a single shared "is anything happening"
  onset gate (the strongest of the three bands this hop) plus a
  same-hop argmax to decide *which* band actually dominates, i.e. which
  instrument it more likely was - the discriminative information is in
  the *relative* distribution across bands at the moment of the hit,
  not in any one band's absolute magnitude.

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

#ifndef PERCUSSIONEVENTDETECTOR_H
#define PERCUSSIONEVENTDETECTOR_H

#include <vector>

#include "audioevents.h"

/** @addtogroup engine_audio Audio
 * @{
 */

class PercussionEventDetector final
{
public:
    /** @param frameRateHz the onset hop rate (BeatOnsetExtractor::frameRateHz()) */
    explicit PercussionEventDetector(double frameRateHz);

    void reset();

    /** Feed one onset hop's per-band rising-edge values (index
     *  0=kick, 1=snare/clap, 2=hihat, matching BeatOnsetExtractor's
     *  band order; used to decide *whether* a hop is a hit at all) and
     *  the same hop's raw, pre-saturation per-band energy (used to
     *  decide *which* band actually dominates it - saturation is
     *  deliberately tuned to equalize kick/snare magnitude for tempo
     *  tracking, see the class comment, so classification needs the
     *  un-equalized signal instead) at audio-timeline timestamp
     *  @a nowSec; appends any events detected in this hop to
     *  @a eventsOut. */
    void push(const double band[3], const double bandEnergy[3], double nowSec,
              std::vector<AudioEvent> &eventsOut);

    /** Diagnostics only, as of the last push() call: the shared gate's
     *  raw peak value (v) and the adaptive threshold it was compared
     *  against - lets a caller show *how close* real audio is getting
     *  to firing, not just whether it fired. */
    double lastPeakValue() const { return m_lastPeak; }
    double lastThreshold() const { return m_lastThreshold; }

private:
    double m_frameRateHz;
    double m_adaptRate;
    double m_refractorySec;

    double m_mean;       // slow-moving noise-floor estimate (shared gate)
    double m_meanDev;    // slow-moving mean absolute deviation
    double m_lastEventSec;

    double m_lastPeak = 0.0;
    double m_lastThreshold = 0.0;
};

/** @} */

#endif // PERCUSSIONEVENTDETECTOR_H
