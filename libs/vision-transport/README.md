# catcheye-vision-transport

처리 결과를 외부로 전달하는 전송 계층 인터페이스 모듈이다.

역할:

- 공통 publisher 인터페이스 정의
- runtime과 실제 전송 구현체 사이 경계 제공

즉 이 모듈은 "어떻게 보낼지"를 책임진다.

기본 전제는 WebSocket 같은 양방향 transport 구현체를 여기에 붙이는 거다.

현재 포함:

- `ResultPublisher`
  - runtime이 호출하는 공통 publish 인터페이스
- `WebSocketPublisher`
  - frame metadata JSON + binary JPEG payload를 WebSocket으로 전송하는 기본 구현체
