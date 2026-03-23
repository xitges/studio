/*
  ==============================================================================

    AutoTuneProcessor.h
    Created: 22 Mar 2026
    Author:  홍준영

    Dedicated real-time vocal pitch correction engine.

    Architecture (staged pipeline):
      1. Input Conditioning — mono downmix, RMS metering, DC block
      2. Voiced/Unvoiced Detection — YIN pitch + confidence + onset
      3. Note-Target Controller — scale snap, note lock, hysteresis
      4. Correction Law — retune speed, correction amount, humanize, flex tune
      5. Transition Engine — glide/portamento between note targets
      6. Vibrato Handler — detect and preserve/reduce natural vibrato
      7. Pitch Shifting — TD-PSOLA (pitch-synchronous overlap-add)
      8. Wet/Dry Output — mix stage

    Pitch shifting uses TD-PSOLA instead of RubberBand for:
      - ~5-10ms latency (vs ~23-46ms with phase vocoder)
      - Characteristic Auto-Tune sound from grain-based processing
      - Continuous formant control (0-1, not binary)
      - Minimal CPU cost (no FFT)

    Audio-thread safe: all buffers pre-allocated in prepare().

  ==============================================================================
*/

#pragma once
#include "ProjectModel.h"
#include "PitchDetector.h"
#include "PSOLAPitchShifter.h"

class AutoTuneProcessor
{
public:
    AutoTuneProcessor();
    ~AutoTuneProcessor();

    AutoTuneProcessor(AutoTuneProcessor&&) noexcept;
    AutoTuneProcessor& operator=(AutoTuneProcessor&&) noexcept;

    void prepare(double sampleRate, int maxBlockSize);
    void processBlock(float* L, float* R, int numSamples, const AutoTuneParams& params);
    void reset();

    // --- UI metering accessors (read from message thread) ---
    float getDetectedPitchHz() const { return detectedPitchHz_; }
    float getTargetPitchHz()   const { return targetPitchHz_; }
    float getInputRms()        const { return inputRms_; }
    float getConfidence()      const { return confidence_; }
    float getCorrectedPitchHz() const { return correctedPitchHz_; }
    bool  isVoiced()           const { return voiced_; }

private:
    double sampleRate_ = 44100.0;
    int    maxBlockSize_ = 512;

    // --- Stage 1: Pitch Detection ---
    PitchDetector pitchDetector_;

    // --- Stage 2: Note-Target Controller ---
    struct NoteTarget
    {
        float targetMidi   = 0.0f;   // current locked target (MIDI note, fractional)
        float targetHz     = 0.0f;
        int   targetNoteInt = -1;    // integer MIDI note of current target
        bool  locked       = false;  // true when we have a stable note lock
        int   lockFrames   = 0;      // how many frames we've been locked on this note
        float entryMidi    = 0.0f;   // pitch when we first locked onto this note
    };
    NoteTarget noteTarget_;

    // Hysteresis: pitch must move more than this (in semitones) to break lock
    static constexpr float kLockBreakSemitones = 0.8f;
    // Minimum frames to hold a note before allowing target change
    static constexpr int   kMinLockFrames = 3;
    // Confidence threshold for note lock
    static constexpr float kLockConfThreshold = 0.4f;

    // --- Stage 3: Correction Law ---
    float correctedMidi_ = 0.0f;

    // --- Stage 4: Transition / Glide ---
    float prevTargetMidi_ = 0.0f;
    float glideProgress_  = 1.0f;

    // --- Stage 5: Vibrato Detection ---
    float vibratoAccum_   = 0.0f;
    float vibratoEnergy_  = 0.0f;
    static constexpr int kVibratoWindowFrames = 12;
    float vibratoHistory_[16] = {};
    int   vibratoHistIdx_ = 0;

    // --- Stage 6: Pitch Shifting (TD-PSOLA) ---
    PSOLAPitchShifter psolaShifter_;
    float currentPitchRatio_ = 1.0f;
    float targetPitchRatio_  = 1.0f;

    // --- Pre-allocated work buffers ---
    std::vector<float> monoBuf_;
    std::vector<float> dryL_, dryR_;
    std::vector<float> psolaOutBuf_;  // PSOLA mono output

    // --- DC blocker state ---
    float dcIn_  = 0.0f;
    float dcOut_ = 0.0f;

    // --- UI metering (relaxed read from message thread) ---
    float detectedPitchHz_  = 0.0f;
    float targetPitchHz_    = 0.0f;
    float correctedPitchHz_ = 0.0f;
    float inputRms_         = 0.0f;
    float confidence_       = 0.0f;
    bool  voiced_           = false;

    // --- Internal methods ---
    float hzToMidi(float hz) const;
    float midiToHz(float midi) const;
    float snapToScale(float midiNote, int keyTonic, int scaleType,
                      const bool* noteMask, bool useNoteMask) const;

    void  updateNoteTarget(float detectedMidi, float confidence,
                           bool isOnset, const AutoTuneParams& params);
    float applyCorrectionLaw(float detectedMidi, float targetMidi,
                             const AutoTuneParams& params) const;
    float applyVibratoHandling(float detectedMidi, float correctedMidi,
                               float vibratoPreserve) const;
    void  updateVibratoDetector(float detectedMidi, float targetMidi);
};
