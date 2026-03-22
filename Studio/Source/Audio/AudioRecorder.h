/*
  ==============================================================================

    AudioRecorder.h
    Created: 21 Mar 2026
    Author:  홍준영

    Lock-free audio recorder.
    - Audio thread pushes samples into AbstractFifo (no alloc, no lock)
    - Background thread pulls from FIFO and writes WAV via AudioFormatWriter

  ==============================================================================
*/

#pragma once
#include <JuceHeader.h>

class AudioRecorder : private juce::Thread
{
public:
    AudioRecorder() : juce::Thread("AudioRecorder") {}
    ~AudioRecorder() override { stopRecording(); }

    //--------------------------------------------------------------------------
    // Call from message thread only
    //--------------------------------------------------------------------------

    /** Begin recording to a WAV file (24-bit, stereo). */
    bool startRecording(const juce::File& outputFile, double sampleRate,
                        int numChannels = 2)
    {
        if (recording_.load(std::memory_order_relaxed))
            return false;

        recordedFile_ = outputFile;
        numChannels_  = juce::jlimit(1, 2, numChannels);
        sampleRate_   = sampleRate;

        // Pre-allocate FIFO ring buffer (~12 s at 44100 Hz stereo)
        const int fifoSamples = 524288;   // power-of-two for efficiency
        fifo_.setTotalSize(fifoSamples);
        fifoBuffer_.resize((size_t)(fifoSamples * numChannels_), 0.0f);

        // Create WAV writer
        juce::WavAudioFormat wav;
        auto stream = outputFile.createOutputStream();
        if (stream == nullptr)
            return false;

        auto* rawStream = stream.release();   // writer takes ownership
        writer_.reset(wav.createWriterFor(
            rawStream, sampleRate_, (unsigned int)numChannels_, 24, {}, 0));
        if (writer_ == nullptr)
        {
            delete rawStream;
            return false;
        }

        totalSamplesWritten_ = 0;
        fifo_.reset();
        recording_.store(true, std::memory_order_release);
        startThread(juce::Thread::Priority::high);
        return true;
    }

    /** Stop recording and finalise the WAV file. */
    void stopRecording()
    {
        if (!recording_.load(std::memory_order_relaxed))
            return;

        recording_.store(false, std::memory_order_release);

        // Wait for writer thread — keep trying until it actually exits.
        // The thread checks threadShouldExit() in its loop.
        if (!stopThread(5000))
        {
            // Thread didn't exit in time — wait more rather than
            // proceeding while the thread still accesses writer_.
            waitForThreadToExit(5000);
        }
        flushFifo();
        writer_.reset();
    }

    bool isRecording() const { return recording_.load(std::memory_order_relaxed); }

    juce::File getRecordedFile() const { return recordedFile_; }

    /** Total frames written so far (approximate — read on message thread). */
    juce::int64 getTotalSamplesWritten() const
    {
        return totalSamplesWritten_.load(std::memory_order_relaxed);
    }

    //--------------------------------------------------------------------------
    // Call from audio thread — RT-safe (lock-free, allocation-free)
    //--------------------------------------------------------------------------

    /** Push input samples into the FIFO. No-op if not recording. */
    void writeBlock(const float* const* inputData, int numInputChannels, int numSamples)
    {
        if (!recording_.load(std::memory_order_acquire))
            return;

        const int ch = juce::jmin(numInputChannels, numChannels_);
        int start1, size1, start2, size2;
        fifo_.prepareToWrite(numSamples, start1, size1, start2, size2);

        // Write interleaved (ch0, ch1, ch0, ch1, …) into ring buffer
        if (size1 > 0)
            writeInterleaved(inputData, ch, start1, size1);
        if (size2 > 0)
            writeInterleaved(inputData, ch, start2, size2, size1);

        fifo_.finishedWrite(size1 + size2);
    }

private:
    //--------------------------------------------------------------------------
    // Background thread — reads FIFO, writes to disk
    //--------------------------------------------------------------------------
    void run() override
    {
        // Temp buffer for writing — allocated once on this thread (not audio thread)
        juce::AudioBuffer<float> tempBuf(numChannels_, 1024);

        while (!threadShouldExit() || fifo_.getNumReady() > 0)
        {
            const int ready = fifo_.getNumReady();
            if (ready == 0)
            {
                wait(5);   // sleep briefly until more data arrives
                continue;
            }

            const int toDo = juce::jmin(ready, 1024);
            int start1, size1, start2, size2;
            fifo_.prepareToRead(toDo, start1, size1, start2, size2);

            if (size1 > 0)
                readInterleaved(tempBuf, start1, size1, 0);
            if (size2 > 0)
                readInterleaved(tempBuf, start2, size2, size1);

            fifo_.finishedRead(size1 + size2);

            const int total = size1 + size2;
            if (total > 0 && writer_ != nullptr)
            {
                writer_->writeFromAudioSampleBuffer(tempBuf, 0, total);
                totalSamplesWritten_.fetch_add(total, std::memory_order_relaxed);
            }
        }
    }

    /** Flush any remaining samples after recording stops. */
    void flushFifo()
    {
        juce::AudioBuffer<float> tempBuf(numChannels_, 1024);
        while (fifo_.getNumReady() > 0)
        {
            const int toDo = juce::jmin(fifo_.getNumReady(), 1024);
            int start1, size1, start2, size2;
            fifo_.prepareToRead(toDo, start1, size1, start2, size2);

            if (size1 > 0) readInterleaved(tempBuf, start1, size1, 0);
            if (size2 > 0) readInterleaved(tempBuf, start2, size2, size1);
            fifo_.finishedRead(size1 + size2);

            const int total = size1 + size2;
            if (total > 0 && writer_ != nullptr)
            {
                writer_->writeFromAudioSampleBuffer(tempBuf, 0, total);
                totalSamplesWritten_.fetch_add(total, std::memory_order_relaxed);
            }
        }
    }

    //--------------------------------------------------------------------------
    // Interleaved helper — audio thread writes, background thread reads
    //--------------------------------------------------------------------------
    void writeInterleaved(const float* const* inputData, int ch,
                          int fifoStart, int count, int srcOffset = 0)
    {
        for (int i = 0; i < count; ++i)
        {
            const int bufIdx = (fifoStart + i) * numChannels_;
            for (int c = 0; c < numChannels_; ++c)
            {
                // Mono input → duplicate to both channels
                const int srcCh = (c < ch) ? c : 0;
                fifoBuffer_[(size_t)bufIdx + (size_t)c] = inputData[srcCh][srcOffset + i];
            }
        }
    }

    void readInterleaved(juce::AudioBuffer<float>& dest,
                         int fifoStart, int count, int destOffset)
    {
        for (int i = 0; i < count; ++i)
        {
            const int bufIdx = (fifoStart + i) * numChannels_;
            for (int c = 0; c < numChannels_; ++c)
                dest.setSample(c, destOffset + i, fifoBuffer_[(size_t)bufIdx + (size_t)c]);
        }
    }

    //--------------------------------------------------------------------------
    juce::AbstractFifo fifo_ { 1 };   // resized in startRecording()
    std::vector<float> fifoBuffer_;
    std::unique_ptr<juce::AudioFormatWriter> writer_;
    std::atomic<bool>  recording_ { false };
    std::atomic<juce::int64> totalSamplesWritten_ { 0 };
    juce::File recordedFile_;
    int  numChannels_ = 2;
    double sampleRate_ = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioRecorder)
};
