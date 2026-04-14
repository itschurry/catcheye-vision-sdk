# CatchEye Vision ROI

`vision-roi`는 ROI 설정, 기하 계산, 검증, 판정을 담당하는 공통 모듈이야.

이 모듈이 하는 일:

- ROI 타입 정의
- ROI JSON 파싱 / 저장
- 폴리곤 기하 계산
- ROI 설정 검증
- 점 / 박스의 ROI 포함 판정

이 모듈이 안 하는 일:

- detector 실행
- 앱별 정책 처리
- preview 렌더링
- transport / websocket 전송

즉 앱은 이 모듈을 써서 ROI를 읽고 검증하고 판정만 하면 돼.
