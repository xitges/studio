/*
  ==============================================================================

    AutoTuneProcessor.cpp
    Created: 22 Mar 2026
    Author:  홍준영

    Dedicated real-time vocal Auto-Tune engine.
    Staged pipeline: detect → target → correct → PSOLA shift → output.

  ==============================================================================
*/

#include "AutoTuneProcessor.h"
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

AutoTuneProcessor::AutoTuneProcessor() = default;
AutoTuneProcessor::~AutoTuneProcessor() = default;

AutoTuneProcessor::AutoTuneProcessor(AutoTuneProcessor&&) noexcept = default;
AutoTuneProcessor& AutoTuneProcessor::operator=(AutoTuneProcessor&&) noexcept = default;

// ---------------------------------------------------------------------------
// prepare / reset
// ---------------------------------------------------------------------------

void AutoTuneProcessor::prepare(double sampleRate, int maxBlockSize)
{
    sampleRate_   = sampleRate;
    maxBlockSize_ = maxBlockSize;

    pitchDetector_.prepare(sampleRate, maxBlockSize);
    psolaShifter_.prepare(sampleRate, maxBlockSize);

    // Pre-allocate work buffers
    const size_t sz = (size_t)maxBlockSize;
    monoBuf_.resize(sz, 0.0f);
    dryL_.resize(sz, 0.0f);
    dryR_.resize(sz, 0.0f);
    psolaOutBuf_.resize(sz, 0.0f);

    // Reset all state
    currentPitchRatio_ = 1.0f;
    targetPitchRatio_  = 1.0f;
    correctedMidi_     = 0.0f;
    prevTargetMidi_    = 0.0f;
    glideProgress_     = 1.0f;
    dcIn_  = 0.0f;
    dcOut_ = 0.0f;

    noteTarget_ = {};
    vibratoAccum_ = 0.0f;
    vibratoEnergy_ = 0.0f;
    vibratoHistIdx_ = 0;
    for (int i = 0; i < 16; ++i) vibratoHistory_[i] = 0.0f;
}

void AutoTuneProcessor::reset()
{
    pitchDetector_.reset();
    psolaShifter_.reset();

    if (!dryL_.empty())      std::fill(dryL_.begin(), dryL_.end(), 0.0f);
    if (!dryR_.empty())      std::fill(dryR_.begin(), dryR_.end(), 0.0f);
    if (!psolaOutBuf_.empty()) std::fill(psolaOutBuf_.begin(), psolaOutBuf_.end(), 0.0f);

    currentPitchRatio_ = 1.0f;
    targetPitchRatio_  = 1.0f;
    correctedMidi_     = 0.0f;
    prevTargetMidi_    = 0.0f;
    glideProgress_     = 1.0f;
    detectedPitchHz_   = 0.0f;
    targetPitchHz_     = 0.0f;
    correctedPitchHz_  = 0.0f;
    inputRms_          = 0.0f;
    confidence_        = 0.0f;
    voiced_            = false;
    dcIn_  = 0.0f;
    dcOut_ = 0.0f;

    noteTarget_ = {};
    vibratoAccum_ = 0.0f;
    vibratoEnergy_ = 0.0f;
    vibratoHistIdx_ = 0;
    for (int i = 0; i < 16; ++i) vibratoHistory_[i] = 0.0f;
}

// ---------------------------------------------------------------------------
// Pitch helpers
// ---------------------------------------------------------------------------

float AutoTuneProcessor::hzToMidi(float hz) const
{
    return 12.0f * std::log2(hz / 440.0f) + 69.0f;
}

float AutoTuneProcessor::midiToHz(float midi) const
{
    return 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f);
}

// ---------------------------------------------------------------------------
// Scale snapping — supports per-note mask
// ---------------------------------------------------------------------------

float AutoTuneProcessor::snapToScale(float midiNote, int keyTonic, int scaleType,
                                     const bool* noteMask, bool useNoteMask) const
{
    if (useNoteMask && noteMask != nullptr)
    {
        const int roundedMidi = (int)std::round(midiNote);
        const int pc = ((roundedMidi % 12) + 12) % 12;

        if (noteMask[pc])
            return (float)roundedMidi;

        for (int offset = 1; offset <= 6; ++offset)
        {
            const int above = ((pc + offset) % 12 + 12) % 12;
            const int below = ((pc - offset) % 12 + 12) % 12;

            const float aboveMidi = (float)(roundedMidi + offset);
            const float belowMidi = (float)(roundedMidi - offset);

            bool aboveOk = noteMask[above];
            bool belowOk = noteMask[below];

            if (aboveOk && belowOk)
            {
                return (std::abs(midiNote - aboveMidi) <= std::abs(midiNote - belowMidi))
                     ? aboveMidi : belowMidi;
            }
            if (aboveOk) return aboveMidi;
            if (belowOk) return belowMidi;
        }
        return (float)(int)std::round(midiNote);
    }

    const int roundedMidi = (int)std::round(midiNote);
    KeySignature key;
    key.tonic = keyTonic;
    key.scale = static_cast<ScaleType>(scaleType);
    return (float)MusicTheory::snapPitchToScale(roundedMidi, key);
}

// ---------------------------------------------------------------------------
// STAGE 2: Note-Target Controller
// ---------------------------------------------------------------------------

void AutoTuneProcessor::updateNoteTarget(float detectedMidi, float confidence,
                                         bool isOnset, const AutoTuneParams& params)
{
    const float snappedMidi = snapToScale(detectedMidi, params.keyTonic, params.scaleType,
                                          params.noteMask, params.useNoteMask);
    const int snappedNote = (int)std::round(snappedMidi);

    // Case 1: No current lock — acquire if confident enough
    if (!noteTarget_.locked)
    {
        if (confidence >= kLockConfThreshold)
        {
            if (noteTarget_.targetMidi > 0.0f)
            {
                prevTargetMidi_ = noteTarget_.targetMidi;
                glideProgress_ = 0.0f;
            }

            noteTarget_.targetMidi    = snappedMidi;
            noteTarget_.targetHz      = midiToHz(snappedMidi);
            noteTarget_.targetNoteInt = snappedNote;
            noteTarget_.locked        = true;
            noteTarget_.lockFrames    = 0;
            noteTarget_.entryMidi     = detectedMidi;
        }
        return;
    }

    // Case 2: Locked — check for re-targeting
    noteTarget_.lockFrames++;

    // On onset, allow immediate re-targeting
    if (isOnset && confidence >= kLockConfThreshold)
    {
        if (snappedNote != noteTarget_.targetNoteInt)
        {
            prevTargetMidi_ = noteTarget_.targetMidi;
            glideProgress_ = 0.0f;

            noteTarget_.targetMidi    = snappedMidi;
            noteTarget_.targetHz      = midiToHz(snappedMidi);
            noteTarget_.targetNoteInt = snappedNote;
            noteTarget_.lockFrames    = 0;
            noteTarget_.entryMidi     = detectedMidi;
        }
        return;
    }

    // Case 3: Sustained — only change if drifted past hysteresis threshold
    if (noteTarget_.lockFrames >= kMinLockFrames && confidence >= kLockConfThreshold)
    {
        const float drift = std::abs(detectedMidi - noteTarget_.targetMidi);

        if (drift > kLockBreakSemitones && snappedNote != noteTarget_.targetNoteInt)
        {
            prevTargetMidi_ = noteTarget_.targetMidi;
            glideProgress_ = 0.0f;

            noteTarget_.targetMidi    = snappedMidi;
            noteTarget_.targetHz      = midiToHz(snappedMidi);
            noteTarget_.targetNoteInt = snappedNote;
            noteTarget_.lockFrames    = 0;
            noteTarget_.entryMidi     = detectedMidi;
        }
    }
}

// ---------------------------------------------------------------------------
// STAGE 3: Correction Law
// ---------------------------------------------------------------------------

float AutoTuneProcessor::applyCorrectionLaw(float detectedMidi, float targetMidi,
                                            const AutoTuneParams& params) const
{
    const float error = targetMidi - detectedMidi;
    const float absError = std::abs(error);

    // Flex Tune: reduce correction when pitch is near target center
    // Uses smoothstep for more natural transition at zone boundary
    float flexFactor = 1.0f;
    if (params.flexTune > 0.0f)
    {
        const float flexWindowSt = params.flexTune * 0.5f;  // max half-semitone window
        if (absError < flexWindowSt)
        {
            const float t = absError / flexWindowSt;
            // Smoothstep: smoother than quadratic at boundaries
            flexFactor = t * t * (3.0f - 2.0f * t);
        }
    }

    // Correction amount scaled by flex
    const float corrStrength = params.correctionAmount * flexFactor;

    // Humanize: scaled natural deviation pass-through
    // Uses soft curve so small deviations pass more than large ones
    float humanizeBypass = 0.0f;
    if (params.humanize > 0.0f && absError > 0.001f)
    {
        // Max deviation window: humanize=1 allows up to 0.3 semitones through
        const float maxDev = params.humanize * 0.3f;
        // Soft saturation: tanh-like curve (cheaper approximation)
        const float x = absError / maxDev;
        const float softDev = maxDev * (x / (1.0f + x));  // hyperbolic soft-clip
        humanizeBypass = (error > 0.0f) ? softDev : -softDev;
    }

    return detectedMidi + error * corrStrength - humanizeBypass;
}

// ---------------------------------------------------------------------------
// STAGE 5: Vibrato Detection & Handling
// ---------------------------------------------------------------------------

void AutoTuneProcessor::updateVibratoDetector(float detectedMidi, float targetMidi)
{
    const float deviation = detectedMidi - targetMidi;
    const int idx = vibratoHistIdx_ % kVibratoWindowFrames;

    vibratoHistory_[idx] = deviation;
    vibratoHistIdx_++;

    const int n = std::min(vibratoHistIdx_, kVibratoWindowFrames);
    if (n < 4) { vibratoEnergy_ = 0.0f; vibratoAccum_ = deviation; return; }

    // Compute mean and variance (vibrato depth estimate)
    float sum = 0.0f, sumSq = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        sum += vibratoHistory_[i];
        sumSq += vibratoHistory_[i] * vibratoHistory_[i];
    }
    const float mean = sum / (float)n;
    const float variance = (sumSq / (float)n) - (mean * mean);
    vibratoEnergy_ = std::max(0.0f, variance);

    // Vibrato depth: RMS of deviation (in semitones)
    vibratoDepthSt_ = std::sqrt(vibratoEnergy_);

    // Zero-crossing rate → vibrato rate estimate
    // Typical vibrato: 4-8 Hz. At ~86 Hz update rate, that's ~10-21 frames per cycle
    int zeroCrossings = 0;
    for (int i = 1; i < n; ++i)
    {
        const int prev = (idx - n + i + kVibratoWindowFrames) % kVibratoWindowFrames;
        const int curr = (prev + 1) % kVibratoWindowFrames;
        if ((vibratoHistory_[prev] - mean) * (vibratoHistory_[curr] - mean) < 0.0f)
            zeroCrossings++;
    }
    // Each full cycle has 2 zero crossings
    const float updateRateHz = (float)(sampleRate_ / 512.0); // approximate analysis rate
    vibratoRateHz_ = (zeroCrossings > 0)
        ? (float)zeroCrossings * 0.5f * updateRateHz / (float)n
        : 0.0f;

    // Extract the AC component (vibrato oscillation), remove DC (mean drift)
    vibratoAccum_ = deviation - mean;
}

float AutoTuneProcessor::applyVibratoHandling(float detectedMidi, float correctedMidi,
                                              float vibratoPreserve) const
{
    if (vibratoPreserve <= 0.001f)
        return correctedMidi;

    // Only preserve vibrato if it looks like actual vibrato:
    // - Depth between 0.05 and 2.0 semitones
    // - Rate between 3 and 12 Hz
    const bool isVibrato = vibratoDepthSt_ > 0.05f && vibratoDepthSt_ < 2.0f
                        && vibratoRateHz_ > 2.5f && vibratoRateHz_ < 12.0f;

    if (!isVibrato || vibratoEnergy_ < 0.001f)
        return correctedMidi;

    // Add back the vibrato AC component scaled by preserve amount
    // Clamp the contribution to avoid extreme jumps
    const float maxVibSt = 1.5f;
    const float vibComponent = std::max(-maxVibSt, std::min(vibratoAccum_, maxVibSt));
    return correctedMidi + vibComponent * vibratoPreserve;
}

// ---------------------------------------------------------------------------
// processBlock — called from audio thread
// ---------------------------------------------------------------------------

void AutoTuneProcessor::processBlock(float* L, float* R, int numSamples,
                                     const AutoTuneParams& params)
{
    if (!params.enabled || numSamples <= 0)
        return;

    // === Update pitch detector frequency range ===
    pitchDetector_.setFrequencyRange(params.inputLowHz, params.inputHighHz);

    // === STAGE 1: Input Conditioning ===
    float energy = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        dryL_[(size_t)i] = L[i];
        dryR_[(size_t)i] = R[i];

        // Mono mix with DC blocker (~5 Hz high-pass)
        float mono = (L[i] + R[i]) * 0.5f;
        const float dcCoef = 0.9995f;
        dcOut_ = mono - dcIn_ + dcCoef * dcOut_;
        dcIn_ = mono;
        mono = dcOut_;

        monoBuf_[(size_t)i] = mono;
        energy += mono * mono;
    }
    inputRms_ = std::sqrt(energy / (float)numSamples);

    // === STAGE 2: Pitch Detection ===
    pitchDetector_.processBlock(monoBuf_.data(), numSamples);
    const float detectedHz  = pitchDetector_.getSmoothedPitchHz();
    const float rawConf     = pitchDetector_.getConfidence();
    const bool  isOnset     = pitchDetector_.isOnset();
    const bool  isVoicedNow = pitchDetector_.isVoiced();

    detectedPitchHz_ = detectedHz;
    confidence_      = rawConf;
    voiced_          = isVoicedNow;

    // === STAGES 3-6: Note targeting + Correction ===
    bool doCorrection = false;

    if (isVoicedNow && detectedHz > 0.0f
        && !std::isnan(detectedHz) && !std::isinf(detectedHz))
    {
        const float detectedMidi = hzToMidi(detectedHz);

        // Feed pitch period to PSOLA shifter
        const float periodSamples = (float)(sampleRate_ / (double)detectedHz);
        psolaShifter_.setPitchPeriod(periodSamples);

        // Stage 3: Note-Target Controller
        updateNoteTarget(detectedMidi, rawConf, isOnset, params);

        if (noteTarget_.locked)
        {
            float effectiveTarget = noteTarget_.targetMidi;

            // Transition / glide between notes
            if (glideProgress_ < 1.0f && prevTargetMidi_ > 0.0f)
            {
                const float glideRate = (params.transitionSpeed < 0.01f)
                                      ? 1.0f
                                      : (1.0f - params.transitionSpeed) * 0.3f + 0.01f;
                glideProgress_ += glideRate;
                if (glideProgress_ > 1.0f) glideProgress_ = 1.0f;

                // Smoothstep interpolation
                const float t = glideProgress_ * glideProgress_ * (3.0f - 2.0f * glideProgress_);
                effectiveTarget = prevTargetMidi_ + (noteTarget_.targetMidi - prevTargetMidi_) * t;
            }

            targetPitchHz_ = midiToHz(effectiveTarget);

            // Stage 4: Vibrato detection
            updateVibratoDetector(detectedMidi, effectiveTarget);

            // Stage 5: Correction Law
            float corrected = applyCorrectionLaw(detectedMidi, effectiveTarget, params);

            // Stage 6: Vibrato handling
            corrected = applyVibratoHandling(detectedMidi, corrected, params.vibratoPreserve);

            correctedPitchHz_ = midiToHz(corrected);

            // Update corrected MIDI (retune speed is now applied via per-sample
            // ratio smoothing in Stage 7 — here we just set the target directly)
            correctedMidi_ = corrected;

            // Compute pitch ratio
            float ratio = midiToHz(correctedMidi_) / detectedHz;
            ratio = std::max(0.5f, std::min(ratio, 2.0f));
            targetPitchRatio_ = ratio;
            doCorrection = true;
        }
        else
        {
            targetPitchHz_ = 0.0f;
            targetPitchRatio_ = 1.0f;
        }
    }
    else
    {
        // Unvoiced
        targetPitchHz_ = 0.0f;
        correctedPitchHz_ = 0.0f;
        targetPitchRatio_ = 1.0f;

        noteTarget_.locked = false;
        noteTarget_.lockFrames = 0;

        vibratoAccum_ = 0.0f;
        vibratoEnergy_ = 0.0f;
    }

    // === Compute per-sample ratio smoothing coefficient from retune speed ===
    // retuneSpeed 0.0 → instant (~1ms), 1.0 → very slow (~500ms)
    // Map to smoothing time constant in samples
    {
        const float speed = std::max(0.0f, std::min(params.retuneSpeed, 0.999f));
        // Exponential mapping: 0→0.5ms, 0.5→30ms, 1.0→500ms
        const float timeMs = 0.5f + 499.5f * speed * speed * speed;
        const float timeSamples = timeMs * 0.001f * (float)sampleRate_;
        // One-pole coefficient: reaches ~63% in timeSamples
        ratioSmoothCoef_ = (timeSamples > 1.0f) ? (1.0f / timeSamples) : 1.0f;
    }

    // === STAGE 7: TD-PSOLA Pitch Shift with per-sample ratio interpolation ===
    // Process mono input through PSOLA with smoothed ratio per sample
    for (int i = 0; i < numSamples; ++i)
    {
        // Per-sample exponential smoothing of pitch ratio (anti-zipper)
        currentPitchRatio_ += (targetPitchRatio_ - currentPitchRatio_) * ratioSmoothCoef_;
    }

    // PSOLA processes the whole block with the final smoothed ratio
    psolaShifter_.processBlock(monoBuf_.data(), psolaOutBuf_.data(), numSamples,
                               currentPitchRatio_, params.formantAmount, isVoicedNow && doCorrection);

    // === STAGE 8: Wet/Dry Output ===
    const float wet = std::max(0.0f, std::min(params.mix, 1.0f));
    const float dry = 1.0f - wet;

    for (int i = 0; i < numSamples; ++i)
    {
        const float wetSample = psolaOutBuf_[(size_t)i];
        L[i] = dryL_[(size_t)i] * dry + wetSample * wet;
        R[i] = dryR_[(size_t)i] * dry + wetSample * wet;
    }
}
