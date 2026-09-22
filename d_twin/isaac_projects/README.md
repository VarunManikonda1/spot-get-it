# d_twin/isaac_projects

Isaac Sim 씬 파일 및 실행 스크립트 관리.

---

## 파일 목록

### launch_isaac.sh

**역할**: `LD_LIBRARY_PATH`에 robot_interfaces 라이브러리 경로를 주입한 뒤 Isaac Sim을 ROS2 브릿지 활성화 상태로 실행.

**반드시 이 스크립트로 실행해야 하는 이유**: Script Node가 robot_interfaces `.so` 파일을 로드하려면 `LD_LIBRARY_PATH`에 해당 경로가 포함되어 있어야 한다. 일반 실행 시 Isaac Sim이 해당 경로를 상속하지 않아 로드 실패.

```bash
#!/bin/bash
export LD_LIBRARY_PATH=~/robot_ws_311/install/robot_interfaces/lib:$LD_LIBRARY_PATH
~/isaac-sim/isaac-sim.sh --enable isaacsim.ros2.bridge
```

**실행**
```bash
SPOT_GET_IT/d_twin/isaac_projects/launch_isaac.sh
```

---

### scenes/dt_env_v3.usd

**역할**: 시연장 환경 및 Spot prim이 배치된 Isaac Sim 씬 파일.

**Stage 구조**
```
World (defaultPrim)
└── MapRoot
    └── SpawnPoints
        ├── Spot_Real        ← robot_id: 1 (실물 로봇)
        ├── Spot_Sim_01      ← robot_id: 2 (sim_02)
        └── Spot_Sim_02      ← robot_id: 3 (sim_03)
```

**Action Graph 구성**

| 노드 | 역할 |
|------|------|
| On Playback Tick | 매 프레임 실행 트리거 |
| Script Node | robot_localization.py 연결 |

연결: `On Playback Tick.tick → Script Node.execIn`

Script Node 설정:
- Use Script File: ✅
- Script File Path: `SPOT_GET_IT/d_twin/scripts/robot_localization.py`

**Spot prim 설정**
- Articulation Enabled: **체크 해제** (물리 엔진 위치 덮어쓰기 방지)
- Kinematic은 `robot_localization.py`에서 Play 시 자동 적용

**좌표계**
- 원점: 시연장 좌측 하단
- +X: 문 벽 방향
- +Y: 단상 방향
- 단위: m
