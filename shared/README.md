# shared

여러 빌드 도메인이 공유하는 자원.

## 하위 구성
- `proto/` — 🔒 STM32↔Jetson 바이너리 시리얼 프로토콜
  - `schema/` — 단일 원본 (.yaml 또는 .proto)
  - `generate.py` — C/Python 바인딩 자동 생성
  - `c/`, `python/` — 생성된 바인딩
- `robot_config/` — 로봇 물리 파라미터
  - `urdf/` — Spot Micro URDF
  - `params.yaml` — 링크 길이, 질량 등

## 주의
`shared/proto/` 수정 시 HW + 통신 + 관제 3명+ 리뷰 필수
(Git Flow 컨벤션 참조)

담당: HW (robot_config/), 통신 (proto/)