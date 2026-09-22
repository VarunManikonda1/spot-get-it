# SPOT_GET_IT

커스텀 Spot Micro 기반 험지탐사 정찰 4족 로봇.
STM32F446RE가 FreeRTOS로 실시간 관절 제어를 담당하고,
Jetson Orin Nano가 ROS2 Humble로 인식·측위·경로 계획·행동 결정을 수행한다.

## 기술 스택
STM32F446RE · FreeRTOS · Jetson Orin Nano · ROS2 Humble · LiDAR · Depth Camera · RL

## 저장소 구조
| 폴더 | 역할 |
|------|------|
| `firmware/` | STM32 펌웨어 |
| `robot_ws/` | Jetson ROS2 워크스페이스 |
| `ai_training/` | RL · Vision 학습 파이프라인 |
| `d_twin/` | 디지털 트윈 |
| `rpi/` | Raspberry Pi 관제 디바이스 |
| `shared/` | STM32↔Jetson 프로토콜, URDF |
| `hardware/` | 회로도, PCB, CAD |
| `docs/` | 아키텍처·프로토콜·컨벤션 문서 |
| `tools/` | 캘리브레이션, 로그 분석, 벤치마크 |

## 팀 구성
- 제영호 (jeayoungho97)
- 한승현 (tmdgus1dnl)
- 방인규 (Swanut97)
- 배경근 (bae-g-g)
- 김병우 (bw522)
- 구진영 (koo001602)