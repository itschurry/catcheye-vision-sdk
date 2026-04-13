# catcheye-vision-input

여긴 공용 입력 모듈이다.

역할은 딱 하나야.

- 이미지, 비디오, 카메라 같은 입력 소스를 `FrameSource` 인터페이스로 통일해서 제공한다.

즉 이 모듈은 "어디서 프레임을 받아올지"만 책임진다.

현재 포함 기능:

- 이미지 파일 입력
- 비디오 파일 입력
- OpenCV `VideoCapture` 기반 카메라 입력
- 기본 카메라 pipeline 문자열 제공
- 입력 소스 factory 제공

공개 API:

- `catcheye::input::Frame`
- `catcheye::input::Timestamp`
- `catcheye::input::PixelFormat`
- `catcheye::input::FrameReadStatus`
- `catcheye::input::FrameSource`
- `catcheye::input::InputSourceConfig`
- `catcheye::input::InputSourceType`
- `catcheye::input::OpenCvCaptureSource`
- `catcheye::input::ImageFileSource`
- `catcheye::input::create_frame_source()`
- `catcheye::input::default_camera_pipeline()`

이 모듈이 안 하는 일:

- detector 실행
- ROI 판정
- preview 렌더링
- stream 전송
- 프레임 루프 제어
- 앱별 CLI/설정 처리

의도한 사용 방식:

- 상위 앱이나 runtime 라이브러리가 `FrameSource`를 생성해서 사용한다.
- 입력 타입이 바뀌어도 상위 로직은 `Frame`만 받으면 되게 만드는 게 목적이다.
