# catcheye-vision-detection

공통 detection 타입과 detector 백엔드 구현을 제공한다.

현재 포함:

- `IDetector`
- `Detection`
- `BoundingBox`
- `NcnnDetector`

상위 앱은 이 모듈의 인터페이스만 보고 detector를 사용한다.
NCNN, Hailo 같은 백엔드 의존성은 이 모듈 안에 둔다.
