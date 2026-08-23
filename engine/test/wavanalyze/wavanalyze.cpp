/*
  Q Light Controller Plus
  wavanalyze.cpp

  Throwaway diagnostic tool (not part of the normal test suite / not
  registered in test.sh): feeds a 16-bit PCM WAV file through the real
  BeatTracker, in the exact same block size AudioCapture uses, and
  prints kickBandOnset/inBreak per block - so a real recorded clip can
  be checked against the actual production algorithm instead of an
  approximation, and without depending on screenshot timing of a live
  running instance.

  Usage: wavanalyze <file.wav> [breakFloor] [breakHoldMs]

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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>

#include "beattracker.h"

#define BLOCK_FRAMES 2048 // AudioCapture's per-channel block size

struct WavData
{
    int sampleRate = 0;
    int channels = 0;
    std::vector<int16_t> samples; // interleaved
};

static bool loadWav(const std::string &path, WavData &out)
{
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
    {
        std::fprintf(stderr, "cannot open %s\n", path.c_str());
        return false;
    }

    char riff[4]; std::fread(riff, 1, 4, f);
    uint32_t chunkSize; std::fread(&chunkSize, 4, 1, f);
    char wave[4]; std::fread(wave, 1, 4, f);
    if (std::strncmp(riff, "RIFF", 4) != 0 || std::strncmp(wave, "WAVE", 4) != 0)
    {
        std::fprintf(stderr, "not a RIFF/WAVE file\n");
        std::fclose(f);
        return false;
    }

    uint16_t bitsPerSample = 16;
    bool haveFmt = false, haveData = false;
    while (!std::feof(f))
    {
        char id[4];
        if (std::fread(id, 1, 4, f) != 4) break;
        uint32_t size;
        if (std::fread(&size, 4, 1, f) != 1) break;

        if (std::strncmp(id, "fmt ", 4) == 0)
        {
            uint16_t audioFormat, numChannels, blockAlign;
            uint32_t sampleRate, byteRate;
            std::fread(&audioFormat, 2, 1, f);
            std::fread(&numChannels, 2, 1, f);
            std::fread(&sampleRate, 4, 1, f);
            std::fread(&byteRate, 4, 1, f);
            std::fread(&blockAlign, 2, 1, f);
            std::fread(&bitsPerSample, 2, 1, f);
            long extra = long(size) - 16;
            if (extra > 0) std::fseek(f, extra, SEEK_CUR);
            out.channels = numChannels;
            out.sampleRate = int(sampleRate);
            haveFmt = true;
        }
        else if (std::strncmp(id, "data", 4) == 0)
        {
            size_t n = size / sizeof(int16_t);
            out.samples.resize(n);
            std::fread(out.samples.data(), sizeof(int16_t), n, f);
            haveData = true;
        }
        else
        {
            std::fseek(f, long(size), SEEK_CUR);
        }
        if (size % 2 == 1) std::fseek(f, 1, SEEK_CUR); // chunks are word-aligned
    }
    std::fclose(f);

    if (!haveFmt || !haveData)
    {
        std::fprintf(stderr, "missing fmt/data chunk\n");
        return false;
    }
    if (bitsPerSample != 16)
    {
        std::fprintf(stderr, "only 16-bit PCM supported, got %d-bit\n", bitsPerSample);
        return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: %s <file.wav> [breakFloor=0.03] [breakHoldMs=2500]\n", argv[0]);
        return 1;
    }

    double breakFloor = (argc > 2) ? std::atof(argv[2]) : 0.03;
    double breakHoldMs = (argc > 3) ? std::atof(argv[3]) : 2500.0;

    WavData wav;
    if (!loadWav(argv[1], wav))
        return 1;

    // Downmix to mono int16 (same approach as BeatTracker::processAudio
    // itself uses internally for stereo, done here explicitly so the
    // per-block feed below always passes channels=1)
    size_t frames = wav.samples.size() / size_t(wav.channels);
    std::vector<int16_t> mono(frames);
    for (size_t i = 0; i < frames; i++)
    {
        int32_t acc = 0;
        for (int c = 0; c < wav.channels; c++)
            acc += wav.samples[i * wav.channels + c];
        mono[i] = int16_t(acc / wav.channels);
    }

    std::printf("Loaded %s: %d Hz, %d ch -> %.2fs mono\n",
                argv[1], wav.sampleRate, wav.channels,
                double(frames) / wav.sampleRate);
    std::printf("breakFloor=%.3f breakHoldMs=%.0f\n\n", breakFloor, breakHoldMs);

    BeatTracker tracker(wav.sampleRate, 1);

    // Mirrors VCMusicReactive's current rolling-beat-window break logic
    // exactly (see slotAudioEventDetected()'s Beat handling and
    // slotAnalysisStateChanged() there) - kept in sync by hand since
    // this is a throwaway diagnostic, not linked against the QML widget.
    static constexpr double kBeatHitFloor = 0.10;
    static constexpr int kBeatWindowSize = 4;
    std::vector<bool> recentBeatHits;
    double audioTimeSec = 0.0;
    bool prevInBreak = false;

    for (size_t s = 0; s + BLOCK_FRAMES <= frames; s += BLOCK_FRAMES)
    {
        std::vector<AudioEvent> events;
        tracker.processAudio(mono.data() + s, BLOCK_FRAMES, events);
        const AudioAnalysisState &st = tracker.analysisState();

        double blockSec = double(BLOCK_FRAMES) / wav.sampleRate;
        audioTimeSec += blockSec;

        for (const AudioEvent &ev : events)
        {
            if (ev.type == AudioEventType::Kick)
            {
                std::printf("    t=%6.2fs  KICK conf=%.2f strength=%.2f beatPhase=%.2f\n",
                            ev.timestampSec, ev.confidence, ev.strength, st.beatPhase);
            }
            else if (ev.type == AudioEventType::Beat)
            {
                bool hit = st.bassEnergy >= kBeatHitFloor;
                recentBeatHits.push_back(hit);
                while (recentBeatHits.size() > size_t(kBeatWindowSize))
                    recentBeatHits.erase(recentBeatHits.begin());
                std::printf("    t=%6.2fs  BEAT bassEnergy=%.4f%s\n",
                            ev.timestampSec, st.bassEnergy, hit ? " [HIT]" : "");
            }
        }

        int hits = 0;
        for (bool h : recentBeatHits)
            if (h) hits++;
        // Full window required, not just half - see the matching comment
        // in VCMusicReactive::slotAnalysisStateChanged() (kept in sync by
        // hand, this is a throwaway diagnostic).
        bool inBreak = recentBeatHits.size() >= size_t(kBeatWindowSize)
                     && hits * 2 < int(recentBeatHits.size());

        if (inBreak != prevInBreak)
        {
            std::printf(">>> t=%6.2fs  inBreak -> %s (beatHits %d/%zu)\n",
                        audioTimeSec, inBreak ? "YES (ambient)" : "no (active)",
                        hits, recentBeatHits.size());
            prevInBreak = inBreak;
        }

        std::printf("t=%6.2fs  kickBandOnset=%.4f  bassEnergy=%.4f  bpm=%.1f  beatPhase=%.2f  beatHits=%d/%zu%s\n",
                    audioTimeSec, st.kickBandOnset, st.bassEnergy, st.bpm, st.beatPhase,
                    hits, recentBeatHits.size(),
                    inBreak ? "  [BREAK]" : "");
    }

    return 0;
}
