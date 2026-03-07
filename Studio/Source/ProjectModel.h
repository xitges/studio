/*
  ==============================================================================

    ProjectModel.h
    Created: 6 Mar 2026
    Author:  홍준영

    FL 스타일 DAW를 위한 기본 데이터 모델:
    - PlayMode (Pattern / Song)
    - Pattern (채널 x 스텝 패턴)
    - PlaylistClip (타임라인 위의 패턴 클립)
    - Project (BPM, 패턴, 플레이리스트 모음)

    순수 데이터 구조만 정의하고, UI/오디오 엔진은 이 모델을 참조만 합니다.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

enum class PlayMode
{
    Pattern,
    Song
};

struct Pattern
{
    int          id         = 0;
    juce::String name       = "Pattern 1";
    int          lengthBars = 1;
    int          stepCount  = 16;  // 실제 사용 스텝 수 (16, 32, 64 ...)

    static constexpr int kMaxChannels = 16;
    static constexpr int kMaxSteps    = 64; // 상한 (필요시 늘릴 수 있음)

    bool steps[kMaxChannels][kMaxSteps] {};
};

struct PlaylistClip
{
    int          id         = 0;
    int          patternId  = 0;
    int          trackIndex = 0;
    float        startBar   = 0.0f;
    float        lengthBars = 1.0f;

    juce::String name;
};

struct Project
{
    double bpm = 140.0;

    std::vector<Pattern>      patterns;
    std::vector<PlaylistClip> playlistClips;
};

