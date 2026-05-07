## Interface
    
레퍼런스 : teenager engineering op-1
  
  그려야 할 컴포넌트 목록
  
  - 메인 셸
      - MainComponent — 루트 컴포넌트. 모든 패널을 담고 레이아웃을 결정.
  - 트랜스포트 / 컨트롤
      - 툴바 — 재생/정지/녹음, BPM, 패턴 셀렉터, 모드 전환
      - 툴바 내 세그먼트 버튼 (Pattern/Song 모드 등)
  - 시퀀싱 / 어레인지먼트
      - 스텝 시퀀서 그리드, 채널 설정
      - 송 타임라인, 클립 배치
      - 피아노 롤 에디터, 노트 편집
  - 믹서 / FX
      - Mixer - 채널 스트립, 볼륨/팬/뮤트/솔로
      - FX Editor Panel — Compressor, Delay, Reverb 파라미터 에디터
      - AutoTune Editor Panel — Auto-Tune 파라미터 에디터
  - 악기 / 사운드
      - Instrument Panel — 채널별 신스/샘플 파라미터 패널
      - SynthEditor Panel — 신시사이저 상세 파라미터 에디터
      - Launchpad Panel — 8×8 패드 그리드
  - 브라우저
      - 샘플 파일 브라우저 - 샘플 파일 탐색 및 드래그
      - 플러그인 브라우저 - VST/AU 플러그인 탐색
  - 인스펙터
      - Inspector Tab Bar — 탭 셀렉터
      - Step Inspector Strip — 스텝별 Velocity, Gate, Probability 편집
  - 라이브 퍼포먼스
      - 라이브 모드 메인 패널
      - 루프 트랙 목록 및 상태 표시
  
  JUCE가 제공하는 그리기 함수들 잘 모름. 클로드 코드 위임. 
  
  - **[waveform 그리기]**
      
      파형을 화면에 그려야 함. 샘플 각 값을 매핑하면 되는 것. 근데 샘플 수 >> 픽셀 수 이기에, 샘플을 묶어야함.
      
      - 공통 드로잉 방식 : min/max downsampling
          
          픽셀 열 하나가 담당하는 샘플 구간을 계산하고, 그 구간 안의 최솟값과
          최댓값을 구하고, 컴포넌트 중심선 기준으로 최댓값은 위로, 최솟값은
          아래로 수직선을 그리는 식으로.
          
          픽셀 열 px 하나:
          
          s0 = px * (totalSamples / pixelWidth)
          
          s1 = (px+1) * (totalSamples / pixelWidth)
          
          → [s0, s1] 구간 샘플을 스캔해 min, max 추출
          
          그리기:
          
          y_top    = midY - max * halfHeight
          
          y_bottom = midY - min * halfHeight
          
          g.drawVerticalLine(x + px, y_top, y_bottom)
          
          구간 안의 피크가 보존되기 때문에 극단적으로 줌아웃해도 파형 형태가 유지됨.
          
      - 파형이 그려지는 부분 :
          - 플레이리스트 클립 :
              - 오디오 클립 / 샘플 파형 : 디코딩된 오디오버퍼를 직접 읽도록. 클립 길이에 해당하는 샘플 수만 사용하도록. 샘플 파라미터 조정 이뤄지면 파라미터 값에 따른 변화 누산하고 적용. 파라미터 조정이 이뤄지면 오버샘플링 해야하나 ?
              - 패턴 클립 파형 : 패턴 데이터 읽어서 신호를 시뮬레이션. 픽셀 너비보다 큰 버퍼를 일단 만들고, 각 채널 스텝과 노트 순회하고 오실레이션 값 누산.
                  
                  스텝 발동 시 각 파라미터별 값 누산하고, 모든 채널 합산. 이후 피크 정규화 이후 픽셀 너비로 min/max 맞추는 방식으로.
                  
                  버퍼 크기를 픽셀 너비보다 크게 만드는 이유는 엔벨로프의 어택 / 릴리즈 곡선처럼 짧은 과도 구간을 픽셀로 축약할 때 뭉개지지 않게 하기 위해.
                  
          - 신디사이저 파형 미리보기 :
              - 신스 파형 : 선택된 오실레이터 파형 직접 계산, 버퍼 저장. 그 위에 ADSR 엔벨로프 곱해서 amplitude 변화 반영.
          - instrument panel : 각 그려진거 그냥 가져오는 방식. 새로 그려야 하는건 없잖아.
          - 문제 : 클립 길이 달라지면? bpm 바뀌면? 파형 계산 다시?
