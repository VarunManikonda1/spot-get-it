# d_twin/ros2

Isaac Sim 디지털 트윈 연동에 필요한 ROS2 패키지 모음.

실물 로봇(spot_01)의 위치는 Jetson에서 발행하고,
시뮬 로봇(spot_02, spot_03)의 위치는 이 패키지들이 생성하여 발행한다.
Isaac Sim은 `/localization/pose`를 구독하여 Spot prim 위치를 반영한다.

---

## 구조

```
ros2/
├── planning/
│   └── global_path_manager/
└── simulation/
    └── sim_executor/
```

## 빌드

각 패키지를 ROS2 워크스페이스 `src/`에 복사한 뒤 해당 워크스페이스에서 빌드한다.

```bash
colcon build --packages-select global_path_manager sim_executor
source install/setup.bash
```

## 실행

```bash
ros2 launch global_path_manager global_path_manager.launch.py
ros2 launch sim_executor sim_executor.launch.py
```

> `global_path_manager`를 먼저 실행해야 한다.
> TRANSIENT_LOCAL QoS로 순서가 바뀌어도 경로는 수신되지만, global_path_manager가 없으면 경로 자체가 발행되지 않는다.
