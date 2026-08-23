/*
  Q Light Controller Plus
  percussioneventdetector.cpp

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

#include <algorithm>
#include <cmath>

#include "percussioneventdetector.h"

namespace {
const AudioEventType kBandEventType[3] =
    { AudioEventType::Kick, AudioEventType::Snare, AudioEventType::HiHat };
}

PercussionEventDetector::PercussionEventDetector(double frameRateHz)
    : m_frameRateHz(frameRateHz > 0.0 ? frameRateHz : 86.1328125)
    , m_adaptRate(0.02) // ~1s time constant at the 86Hz onset hop rate
    , m_refractorySec(0.09) // shortest sane inter-hit interval, any type
{
    reset();
}

void PercussionEventDetector::reset()
{
    m_mean = 0.0;
    m_meanDev = 0.0;
    m_lastEventSec = -1e9;
}

void PercussionEventDetector::push(const double band[3], const double bandEnergy[3],
                                    double nowSec, std::vector<AudioEvent> &eventsOut)
{
    // Shared "is anything happening" gate: the strongest band this hop.
    // See the class-level comment for why this has to be shared rather
    // than three independent per-band thresholds.
    const double v = std::max(band[0], std::max(band[1], band[2]));

    // v (BeatOnsetExtractor's saturated rising-edge value) is
    // mathematically bounded to [0, 2/3] - see processHop()'s
    // sat = v/(v+0.5*ref) with v<=ref. On real, continuously busy music
    // (not just the clean/sparse synthetic test clips) the old 9.0x
    // multiplier let the threshold drift up past that hard ceiling,
    // permanently locking out every future hit (confirmed live: gate
    // read 0.97, above 2/3, with real kicks audibly playing). 7.0x is
    // the lowest multiplier that still passes the false-positive tests
    // (sustainedBass/vocalOnly), but even that isn't guaranteed to stay
    // under the ceiling on a real track's own variance - so cap the
    // threshold outright, comfortably below 2/3, as a hard backstop
    // that no amount of real-world drift can defeat.
    static constexpr double kMaxThreshold = 0.45;
    const double threshold = std::min(kMaxThreshold, m_mean + 7.0 * m_meanDev);
    m_lastPeak = v;
    m_lastThreshold = threshold;
    const bool fire = (v > threshold) && (v > 1e-6)
                    && (nowSec - m_lastEventSec >= m_refractorySec);

    if (fire)
    {
        m_lastEventSec = nowSec;

        // Classify by which band's RAW (pre-saturation) energy
        // dominates this onset. Saturation normalizes each band against
        // its own recent peak so magnitudes stay comparable for tempo
        // tracking (deliberately, see the class comment) - which also
        // means the saturated band[] values themselves are a poor
        // classifier once a band's reference has calibrated to its
        // loudest source. The raw energy is not run through that
        // per-band normalization, so it keeps the actual spectral
        // balance of *this* hit.
        int winner = 0;
        if (bandEnergy[1] > bandEnergy[winner]) winner = 1;
        if (bandEnergy[2] > bandEnergy[winner]) winner = 2;

        const double confidence = (threshold > 1e-9)
            ? std::min(1.0, (v - threshold) / threshold)
            : std::min(1.0, v);
        eventsOut.push_back(AudioEvent{ kBandEventType[winner], nowSec,
                                        confidence, std::min(1.0, v) });
    }

    // Always adapt the noise floor, fired or not - see the header
    // comment / percussioneventdetector.h for why gating this on "only
    // when not firing" is a trap (degenerate zero-threshold lock-up,
    // confirmed by percussionevents_test).
    const double dev = std::fabs(v - m_mean);
    m_mean += m_adaptRate * (v - m_mean);
    m_meanDev += m_adaptRate * (dev - m_meanDev);
}
