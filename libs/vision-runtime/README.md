# catcheye-vision-runtime

여긴 공통 프레임 처리 runtime 모듈이다.

역할은 이거다.

- 입력 소스에서 프레임을 읽는다
- processor lifecycle을 관리한다
- 공통 frame loop를 실행한다
- end-of-stream / read error / user interrupt 종료 규칙을 처리한다
- preview 표시와 외부 preview sink 호출 타이밍을 관리한다

즉 이 모듈은 "프레임 처리 실행 구조"를 책임진다.

핵심 개념:

- `FrameProcessor`
  - 앱별 알고리즘 구현체가 따라야 하는 인터페이스
- `FrameProcessingRunner`
  - 공통 실행 루프
- `PreviewSink`
  - MJPEG streamer 같은 외부 출력 연결점
- `RuntimeConfig`
  - preview, cadence, window 이름 같은 공통 실행 설정

공개 API:

- `catcheye::runtime::ProcessContext`
- `catcheye::runtime::ProcessOutput`
- `catcheye::runtime::FrameProcessor`
- `catcheye::runtime::PreviewSink`
- `catcheye::runtime::RuntimeConfig`
- `catcheye::runtime::FrameProcessingRunner`

이 모듈이 안 하는 일:

- detector 구현
- 사람 검출, 홀 검출 같은 앱별 알고리즘
- ROI 정책
- bbox, circle 같은 앱별 결과 타입 정의
- 앱별 CLI/설정 파일 처리
- 입력기 구현 자체

의도한 사용 방식:

1. 상위 앱이 `catcheye-vision-input`으로 `FrameSource`를 만든다
2. 앱 전용 `FrameProcessor` 구현체를 만든다
3. 필요하면 `PreviewSink` 구현체를 붙인다
4. `FrameProcessingRunner`에 꽂아서 실행한다

즉 새 앱은 공통 루프를 다시 만들지 말고, processor만 바꿔 끼워서 쓰면 된다.
