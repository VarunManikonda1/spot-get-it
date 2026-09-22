# ai_training

AI 모델 학습 파이프라인. colcon 외부에서 독립 관리.

## 하위 구성
- `rl/` — 보행 정책 강화학습 (Isaac Gym)
- `vision/` — 비전 모델 학습 (YOLO, victim detection)

## 산출물
학습 완료된 모델은 ONNX로 export하여 해당 ROS2 노드에 배포:
- RL 정책 → `robot_ws/src/control/motion_controller/`
- Vision 모델 → `robot_ws/src/perception/camera_perception/`

## 주의
- `checkpoints/`, `runs/`, `wandb/`는 gitignore 대상
- 대용량 모델 파일은 Git LFS로 관리

담당: RL (rl/), Depth Camera (vision/)