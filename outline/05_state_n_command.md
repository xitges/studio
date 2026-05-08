# State & Command

프로그램 대표 7개 거시 상태  : 서로 직교하고, 조합이 전체 시스템의 행동을 결정.

---

### 1. TRANSPORT

재생 위치 보존 여부를 기준으로 세 가지 상태를 구분

| 상태    | 조건                                      | 다음 Play 시작 위치     |
|---------|-------------------------------------------|-------------------------|
| STOPPED | `songPlaying=false`, `pausedBarSong < 0`  | patternStartStep (처음) |
| PAUSED  | `songPlaying=false`, `pausedBarSong >= 0` | 저장된 위치에서 재개    |
| PLAYING | `songPlaying=true`                        | —                       |

Stop 시 현재 위치를 `pausedBarSong` / `pausedPatternBeat`에 저장해 PAUSED로 전환한다. StopToStart는 저장 위치를 초기화해 STOPPED로 전환한다.

---

### 2. PLAY MODE

두 가지 재생 엔진 중 어느 쪽을 구동할지 결정

| 상태    | 구동 방식                              |
|---------|----------------------------------------|
| PATTERN | Sequencer가 16th-note step을 구동      |
| SONG    | Timeline 기반 PlaylistClip 순회        |

PlayMode 전환은 반드시 stop()을 선행. Song Mode에서 Pattern Mode로 전환 시 PolySynth 보이스가 ADSR decay 중일 수 있어, 전환 직후 의도치 않은 소리가 발생하게 됨

---

### 3. WAV RECORDING

| 상태      | 조건                              |
|-----------|-----------------------------------|
| IDLE      | AudioRecorder 비활성              |
| RECORDING | AudioRecorder 스레드 실행 중      |

RECORDING 종료 시 `onRecordingFinished` 콜백이 발화하여 플레이리스트에 Audio 클립을 자동 생성한다.

---

### 4. FILE

두 가지 하위 축의 조합으로 표현한다.

| 축        | 값                                       |
|-----------|------------------------------------------|
| 경로 여부 | NO_FILE (`currentFile = {}`) / HAS_FILE  |
| 저장 여부 | CLEAN (`projectDirty=false`) / DIRTY     |

네 가지 조합 중 NO_FILE + DIRTY가 신규 미저장 상태다. Save 커맨드는 이 두 축의 조합을 보고 동작을 분기한다.

---

### 5. LIVE MODE

| 상태 | 동작                                                        |
|------|-------------------------------------------------------------|
| OFF  | 일반 편집/재생 모드. RecordStart → WAV 녹음                 |
| ON   | RecordStart → WAV 차단, LiveLoop arm으로 분기               |

Live Mode를 별도 축으로 분리한 이유는, 같은 RecordStart 커맨드가 모드에 따라 완전히 다른 시스템(WAV recorder vs LiveLoopEngine)을 구동하기 때문이다. 분기를 커맨드 내부에 두는 것보다 상태 축으로 명시하는 쪽이 추후 확장에 유리하다.

---

### 6. LIVE LOOP (채널별 × 16)

채널별로 독립적인 FSM을 가진다.

```
IDLE
 └─[arm()]──▶ ARMED ──[첫 NoteOn]──▶ WAITINGFORBAR
                                            │
                                      [bar 경계]
                                            ▼
                                       RECORDING ──[loopLength 경과]──▶ LOOPING
                                                                             │
                                                              [overdub()]    │
                                                                             ▼
                                                                       OVERDUBBING
                                                                             │
                                                              [loopLength 경과]
                                                                             ▼
                                                                         LOOPING

어느 상태에서든 stop() → IDLE
```

---

### 7. PIANO ROLL RECORD

피아노롤 창이 열려 있을 때만 유효한 FSM이다.

```
IDLE ──[REC ON + Space]──▶ ARMED ──[Space]──▶ IDLE (취소)
                               │
                         [첫 NoteOn → onStartTransport]
                               ▼
                          RECORDING ──[Stop]──▶ IDLE
                          (NoteEvent 실시간 수집)
```

ARMED 상태에서 Space를 다시 누르면 재생 없이 취소된다. RECORDING 진입은 반드시 transport play가 먼저 호출된다.

---

## 2. 커맨드 가용성 

상태와 커맨드 간 비직관적인 상호작용만 정리

```
커맨드              조건                           동작
───────────────────────────────────────────────────────────────────────────
[Space]

                    PLAYING + Song Mode            → PAUSED (위치 저장)
                    PLAYING + Pattern Mode         → PAUSED (beat 저장)
                    PLAYING + Pattern Mode         → STOPPED + 처음으로
                      + 600ms 내 재Space           (더블 스페이스)

                    STOPPED/PAUSED                 → Play (일반 재생)
                    STOPPED/PAUSED + PR창 열림
                      + PR=Armed                  → PR Armed 취소 (Idle)
                    STOPPED/PAUSED + PR창 열림
                      + Trigger ON + REC ON       → PR Armed 진입 (재생 없음)

───────────────────────────────────────────────────────────────────────────
[PlayMode 전환]

                    PLAYING 여부 무관              → stop() 강제 선행
                                                    (isPlaying 체크 없음)

───────────────────────────────────────────────────────────────────────────
[Stop / StopToStart / New / Open]

                    WAV RECORDING 중               → stopRecording() 자동
                                                    선행 후 명령 실행

───────────────────────────────────────────────────────────────────────────
[RecordStart]

                    LIVE MODE = off                → WAV 녹음 시작
                    LIVE MODE = on                 → WAV 차단,
                                                    LiveLoop arm(midiCh)

[RecordStop]

                    LIVE MODE = off                → WAV stopRecording()
                    LIVE MODE = on                 → LiveLoop stop(midiCh)

───────────────────────────────────────────────────────────────────────────
[New]

                    DIRTY                          → AlertWindow 차단
                                                    → 확인 후 실행
                    CLEAN                          → 즉시 실행

───────────────────────────────────────────────────────────────────────────
[Save]

                    HAS_FILE                       → 동일 경로 덮어쓰기
                    NO_FILE                        → SaveAs 로 자동 분기

───────────────────────────────────────────────────────────────────────────
[Export WAV]

                    Pattern Mode                   → activePattern.stepCount
                                                    기준 bar 계산
                    Song Mode                      → playlistClips 전체
                                                    끝 위치 기준 bar 계산
```

---

## 3. 프로젝트 데이터 모델

 Project 구조체로 전체 데이터 상태를 보유
```
Project
├── bpm, playMode, activePatternId
├── keySignature (root + ScaleType)
├── channelCount
├── patterns[]              — Pattern 목록
│     ├── id, name, stepCount, lengthBars, swingAmount
│     ├── variations[]     — steps[][], stepParams, NoteEvents
│     ├── channelVolume/Pan/Pitch[]
│     ├── channelTypes/Names[]
│     ├── synthParams[], samplerParams[]
│     └── channelMixerRouting[]
├── playlistClips[]         — 타임라인 클립 (Pattern / Audio)
├── playlistTracks[]        — 트랙 행
├── mixerTracks[]           — 버스 (volume, pan, mute, solo)
├── fxParams[]             — Compressor / Delay / Reverb
├── launchpadPads[8x8]       — filePath + playMode
├── automationLanes[]       — 파라미터 + breakpoints
└── channelInstrumentPlugins[] — VST/AU (pluginStateBase64)
```



---

## 4. Undo / Redo

모든 편집 작업을 Command 패턴으로 감싸 스택에 쌓는 식으로.


**적용 범위**: 스텝 토글, 플레이리스트 클립(이동/리사이즈/삭제/생성/이름 변경), 자동화 포인트.

**드래그 배치**: 드래그 중 토글된 스텝 전체를 트랜잭션 하나로 묶기.

**히스토리 초기화**: New/Open 시 `reloadProjectIntoUI()`가 `undoManager.clearUndoHistory()`를 호출한다. 이전 프로젝트의 액션이 새 프로젝트 데이터를 참조하는 상황을 막기 위해서다.

---

## 5. 파일 직렬화

형식은 XML (`.studioproj`). `ProjectSerializer` 클래스가 직렬화를 담당한다.

**Save 순서**:
1. `channelRack.saveToPattern(*activePat)` — 현재 채널랙 편집 내용 flush
2. 플러그인 상태 수집: `audioEngine.getPluginState(ch, state)` → base64
3. `ProjectSerializer::save(project, currentFile)` — XML 직렬화 후 쓰기

**Open 순서**:
1. `ProjectSerializer::load()` → `Project` 역직렬화
2. `reloadProjectIntoUI()` — stop → 샘플/클립 재로드 → UI 갱신

**직렬화 원칙**:
- Sparse 저장: 기본값과 동일한 필드는 XML에 생략
- 하위 호환: 모든 `getXxxAttribute()` 에 기본값 명시, legacy 포맷 자동 마이그레이션

---

## 6. 커맨드 연결 구조

UI 컴포넌트는 std::function 콜백만 노출. MainComponent 생성자에서 실제 동작을 연결하고 UI가 ProjectModel이나 AudioEngine을 직접 참조하지 않음.

```
사용자 입력
  → UI 컴포넌트 이벤트 (onClick, mouseDown 등)
  → std::function 콜백 (onStepToggled, onClipMoved 등)
  → MainComponent 람다
      ├── ProjectModel 수정
      ├── undoManager.perform(LambdaAction)
      ├── markDirty()
      └── AudioEngine 동기화 (setStepPattern, updatePatternSnapshot 등)
```
