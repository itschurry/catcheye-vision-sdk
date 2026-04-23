# catcheye-vision-protocol

프레임 처리 결과에 붙는 애플리케이션 레벨 메시지 계약을 정의하는 모듈이다.

역할:

- 처리 결과 메타데이터를 공통 타입으로 표현
- app, runtime, transport 사이에서 같은 메시지 구조를 공유
- 전송 계층이 사용할 수 있는 최소 메시지 필드를 고정

하지 않는 일:

- JPEG 같은 바이너리 payload 인코딩
- WebSocket frame 생성
- 소켓 송신

즉 이 모듈은 "무슨 메타데이터를 전달할지"만 책임진다.
