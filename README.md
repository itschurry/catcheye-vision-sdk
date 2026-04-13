# catcheye-vision-sdk

공통 비전 앱 개발용 SDK 저장소다.

이 저장소는 서로 강하게 엮여서 같이 발전하는 공통 모듈들을 한 군데 모아둔 거야.

현재 포함 모듈:

- `libs/vision-input`
  - 이미지, 비디오, 카메라 입력을 `FrameSource`로 통일한다
- `libs/vision-runtime`
  - 공통 frame loop, processor lifecycle, preview sink 연결, 종료 규칙을 담당한다

의도한 사용 방식:

1. 상위 앱이 `catcheye::vision_input`으로 입력 소스를 만든다
2. 앱 전용 `FrameProcessor` 구현체를 만든다
3. `catcheye::vision_runtime`의 runner에 꽂아서 실행한다

즉 앱마다 입력 루프를 다시 짜지 말고, 공통 SDK 위에 processor만 갈아끼워서 쓰는 구조를 노린다.
