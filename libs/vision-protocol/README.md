# catcheye-vision-protocol

Flutter 같은 외부 클라이언트와 주고받는 결과 메시지 형식을 정의하는 모듈이다.

역할:

- 처리 결과를 공통 메시지 타입으로 표현
- 프레임을 JPEG payload로 인코딩
- metadata JSON과 binary frame payload를 한 구조로 묶기

즉 이 모듈은 "무엇을 보낼지"를 책임진다.
