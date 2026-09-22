# d_twin/scripts

Isaac Sim Script Node에 연결되는 Python 스크립트 모음.

---

## 파일 목록

### robot_localization.py

**역할**: `/localization/robot/state` 토픽을 구독해 Spot prim의 위치와 방향을 매 프레임 갱신.

**Isaac Sim 연결**: Action Graph → Script Node → Script File Path

**동작 흐름**
1. Play 시작 시 robot_interfaces `.so` 파일을 `ctypes`로 직접 로드
2. rclpy 노드 생성 및 `/localization/robot/state` 구독
3. 매 프레임 `rclpy.spin_once()` 실행
4. 수신된 robot_id별로 `PRIM_MAP` 조회 → `xformOp:translate`, `xformOp:orient` 갱신
5. Play 시작 직후 `KINEMATIC = True`이면 모든 Spot prim의 물리를 Kinematic으로 설정 (1회)

**주요 설정 (상단 상수)**

| 상수 | 기본값 | 설명 |
|------|--------|------|
| `Z_HEIGHT` | `0.15` | Spot prim Z 고정값 (m) |
| `KINEMATIC` | `True` | `True`: 물리 차단(고정) / `False`: 물리 활성화 |

**robot_id → prim 매핑**

| robot_id | prim 경로 |
|----------|-----------|
| 1 | `/World/MapRoot/SpawnPoints/Spot_Real` |
| 2 | `/World/MapRoot/SpawnPoints/Spot_Sim_01` |
| 3 | `/World/MapRoot/SpawnPoints/Spot_Sim_02` |

**의존성**
- `robot_ws_311/` — Python 3.11로 빌드된 robot_interfaces (Isaac Sim 내장 Python 호환)
- `ctypes` — `.so` 파일 직접 로드 (Isaac Sim이 LD_LIBRARY_PATH 상속 불안정)

**주의사항**
- Play 시 ScriptNode 보안 경고 팝업 → **Yes** 선택 필수 (매 세션)
- Isaac Sim 실행 전 반드시 `launch_isaac.sh` 사용 (`LD_LIBRARY_PATH` 주입 필요)
