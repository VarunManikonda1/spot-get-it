# SPOT GET IT — Quadruped Search & Rescue Robot

A custom Spot Micro–class quadruped for rough-terrain reconnaissance and victim
search. An **STM32F446RE** runs the 500 Hz joint control loop, an **NVIDIA Jetson
Orin Nano** runs ROS 2 Humble for perception, localization, path planning and the
50 Hz reinforcement-learning locomotion policy, and a **Raspberry Pi 5** acts as
the ground station with a Qt6 dashboard.

```
              ┌───────────────────────────────────────────────┐
              │  Raspberry Pi 5 — ground station              │
              │  bridge_daemon  ⇄  POSIX SHM  ⇄  Qt dashboard │
              └───────▲───────────────────────────┬───────────┘
            UDP 9000  │ image / lidar / odom      │ UDP 9001
            (telemetry)│ state / event / path     │ (commands, heartbeat)
              ┌───────┴───────────────────────────▼───────────┐
              │  Jetson Orin Nano — ROS 2 Humble  (50 Hz)     │
              │  perception → localization → navigation       │
              │           → motion_manager → RL policy (ONNX) │
              └───────────────────────▲───────────┬───────────┘
                      joint feedback  │           │ 12 joint targets (rad)
                          + IMU       │  UART 921600 @ 50 Hz
              ┌───────────────────────┴───────────▼───────────┐
              │  STM32F446RE — real-time control  (500 Hz)    │
              │  validate → PID → safety → SYNC_WRITE         │
              └───────┬───────────────────────────▲───────────┘
             HDSEL    │ 4 buses × 1 Mbps          │ I²C 400 kHz
             UART     ▼                           │
                12 × Feetech STS3215        Bosch BNO055 IMU
```

> **Provenance.** This is a working mirror of
> [`jeayoungho97/SPOT_GET_IT`](https://github.com/jeayoungho97/SPOT_GET_IT)
> (MIT). Original authors are credited in [Credits](#credits); the upstream
> `LICENSE` is preserved verbatim. Large binaries are excluded — see
> [What is not in this repo](#what-is-not-in-this-repo).

---

## Table of contents

1. [Hardware](#hardware)
2. [Repository layout](#repository-layout)
3. [The pipeline at a glance](#the-pipeline-at-a-glance)
4. [Stage 0 — Clone and pick your machine](#stage-0--clone-and-pick-your-machine)
5. [Stage 1 — STM32 firmware](#stage-1--stm32-firmware)
6. [Stage 2 — Jetson ROS 2 workspace](#stage-2--jetson-ros-2-workspace)
7. [Stage 3 — Robot bring-up order](#stage-3--robot-bring-up-order)
8. [Stage 4 — Raspberry Pi 5 ground station](#stage-4--raspberry-pi-5-ground-station)
9. [Stage 5 — RL locomotion training](#stage-5--rl-locomotion-training)
10. [Stage 6 — Vision model training](#stage-6--vision-model-training)
11. [Stage 7 — Isaac Sim digital twin](#stage-7--isaac-sim-digital-twin)
12. [Stage 8 — URDF viewer (no hardware)](#stage-8--urdf-viewer-no-hardware)
13. [Topic map](#topic-map)
14. [Verification with RViz](#verification-with-rviz)
15. [Troubleshooting](#troubleshooting)
16. [What is not in this repo](#what-is-not-in-this-repo)
17. [Credits](#credits)

---

## Hardware

| Part | Model | Qty | Role |
|---|---|---|---|
| MCU | STM32F446RE (Nucleo-64) | 1 | Joint control @ 500 Hz |
| SBC | NVIDIA Jetson Orin Nano (25 W) | 1 | RL inference, perception |
| Ground station | Raspberry Pi 5 | 1 | Bridge daemon + Qt dashboard |
| Servo | Feetech STS3215-C018 (12 V, 30 kg·cm) | 12 | 4 legs × 3 joints |
| IMU | Bosch BNO055 | 1 | Attitude (onboard fusion) |
| LiDAR | CygLiDAR D1 | 1 | 3D point cloud → `/scan_3D` |
| Camera | Luxonis OAK-D Lite | 1 | RGB stream + MJPEG uplink |
| Signal hub | Perfboard | 1 | 4-bus pull-ups, signal fan-out |

Total mass ≈ 2.5 kg. Full pinout, wiring and power budget:
[`hardware/docs/hardware_design.md`](hardware/docs/hardware_design.md) and
[`hardware/docs/power_system.md`](hardware/docs/power_system.md).

### Link budget

| Link | Interface | Rate | Period |
|---|---|---|---|
| STM32 ↔ servos (4 buses in parallel) | HDSEL UART | 1 Mbps | 500 Hz |
| STM32 ↔ BNO055 | I²C fast-mode | 400 kHz | 500 Hz |
| STM32 ↔ Jetson | UART | 921600 | 50 Hz |
| Debug console | USART2 (ST-Link VCP) | 115200 | — |
| Jetson → RPi5 | UDP 9000 | — | telemetry |
| RPi5 → Jetson | UDP 9001 | — | commands |
| PC ↔ RPi5 | UDP 9002 | — | optional |

---

## Repository layout

| Path | Contents |
|---|---|
| `firmware/` | STM32CubeIDE project — servo bus, IMU, PID, safety |
| `robot_ws/src/` | 20 ROS 2 Humble packages (see below) |
| `ai_training/rl/` | Isaac Gym / `legged_gym` + `rsl_rl` policy training |
| `ai_training/vision/` | YOLOv11 person-detection dataset + training |
| `d_twin/` | Isaac Sim digital twin scenes and script nodes |
| `rpi/bridge_app/` | C bridge daemon (UDP ⇄ POSIX shared memory) |
| `rpi/qt_app/` | Qt6 `DisasterControlQt` dashboard |
| `shared/` | STM32 ⇄ Jetson protocol schema, robot config |
| `hardware/docs/` | Hardware design, power system |
| `rviz/` | Eight per-stage RViz verification configs |
| `tools/` | SPI/trot bench scripts |
| `experimenter/` | LiDAR log collectors |
| `urdf/`, `launch/` | Standalone URDF viewer (desktop, no robot) |

ROS 2 packages under `robot_ws/src/`:

```
bringup/robot_bringup          camera + RL/motion aggregate launches
control/actuator_bridge        UART/SPI link to STM32, 50 Hz
control/classic_control        IK + gait-pattern controller
control/locomotion_common      shared kinematics helpers
control/motion_manager         stand / sit / detect motions + target mux
control/rl_locomotion          ONNX policy runner, obs builder, gait phase
experimenter/lidar_collector   PointCloud2 → JSONL recorder
experimenter/video_collector   session video recorder
interfaces/robot_interfaces    16 custom msgs (JointTarget, StmMotion, …)
localization/spot_localization gait odometry, TF chain, mission pose
navigation/spot_navigation     path progress tracker, local path planner
network/camera_stream_sender   MJPEG → RPi5
network/cmd_receiver           RPi5/BLE command intake
network/lidar_sparse           point-cloud decimation
network/lidar_stream_sender    LiDAR → RPi5
network/network_bringup        aggregate network launch
network/pose_path_event_sender pose / path / event uplink
perception/camera_perception   YOLOv11 person detection (TensorRT or ONNX)
perception/lidar_perception    preprocess, clustering, occupancy, free space
vendor/oak_camera_driver       OAK-D Lite capture + MJPEG UDP
```

---

## The pipeline at a glance

```
 ┌── OFFLINE (workstation) ──────────────────────────────────────────┐
 │  Stage 5  Isaac Gym RL training ──► model_final.pt ──► .onnx      │
 │  Stage 6  YOLOv11 training      ──► best.pt      ──► best.engine  │
 └───────────────────────────┬───────────────────────────────────────┘
                             │ copy artifacts into the repo
 ┌── ON THE ROBOT ───────────▼───────────────────────────────────────┐
 │  Stage 1  flash STM32, calibrate the 12 servo zero points         │
 │  Stage 2  colcon build the Jetson workspace                       │
 │  Stage 3  launch: perception → localization → navigation          │
 │                   → control → network                             │
 └───────────────────────────┬───────────────────────────────────────┘
                             │ UDP 9000 / 9001
 ┌── GROUND STATION ─────────▼───────────────────────────────────────┐
 │  Stage 4  bridge_daemon, then DisasterControlQt                   │
 └───────────────────────────────────────────────────────────────────┘

 Stage 7 (Isaac Sim twin) and Stage 8 (URDF viewer) are optional and
 need no robot hardware.
```

**Hard ordering rules.** Within Stage 3 the order is not cosmetic:

- `obstacle_memory_grid_node` and `memory_fusion_node` consume
  `/localization/pose`, so localization must already be publishing.
- `local_path_planner_node` consumes `/perception/lidar/obstacle_model` and
  `/perception/lidar/free_space_model`, so LiDAR perception must already be
  running.
- `actuator_bridge_node` must come up last: it starts driving real servos the
  moment it sees a target.

---

## Stage 0 — Clone and pick your machine

```bash
git clone https://github.com/VarunManikonda1/spot-get-it.git
cd spot-get-it
```

| Machine | OS | Needs |
|---|---|---|
| Jetson Orin Nano | Ubuntu 22.04 aarch64 | ROS 2 Humble, JetPack, TensorRT |
| Raspberry Pi 5 | ARM64 Linux | gcc, Qt 6.5+, CMake 3.19+ |
| Firmware PC | any | STM32CubeIDE, ST-Link |
| Training PC | Ubuntu + NVIDIA GPU | Isaac Gym, PyTorch, Ultralytics |
| Desktop (Stage 8 only) | Ubuntu + ROS 2 | `robot_state_publisher`, `rviz2` |

---

## Stage 1 — STM32 firmware

The STM32 is **not** a passthrough. It validates each command, runs its own PID
loop at 500 Hz, enforces joint limits / watchdog / temperature, and holds the
last Jetson command across the nine control cycles between 50 Hz updates
(last-command-wins).

```
Jetson (50 Hz)          STM32 (500 Hz)                  Hardware
──────────────          ──────────────                  ────────
RL inference  ──UART──► ① command validation
                        ② PID (joint angle → servo cmd)
                        ③ safety (limit, watchdog, temp)
                        ④ SYNC_WRITE       ──HDSEL──►  12 × STS3215
                        ⑤ bulk read        ◄───────────────┘
                        ⑥ IMU read         ◄───I²C────  BNO055
        ◄──── obs ───── ⑦ state packing
```

### 1.1 Build and flash

1. `File ▸ Import ▸ Existing Projects into Workspace` → select `firmware/`.
2. `Project ▸ Build All` (`Ctrl+B`).
3. `Run ▸ Run` (`Ctrl+F11`) — flashes over ST-Link.

Sources live in `firmware/Src` and `firmware/Inc`
(`servo_sts3215.c`, `imu_bno055.c`, `control_loop.c`, `leg_ik.c`, `gait.c`,
`uart_jetson.c`, `spi_protocol.c`, `calibration.c`, `telemetry.c`).
`ST_0516.ioc` is the CubeMX project.

### 1.2 Debug console

`printf` goes out on USART2 over the ST-Link VCP at `115200 8N1`:

```bash
screen /dev/ttyACM0 115200      # Linux / macOS
```

Press **ESC** at any time for emergency stop — torque off on all 12 servos, then
halt.

### 1.3 Servo zero-point calibration (do this once, before any gait)

Use the Feetech URT-2 tool, then persist the offset. STS3215 `EEPROM Lock`
(`0x37`) must be released first or `Position Offset` (`0x1F`) only lands in RAM
and is lost on the next power cycle:

```
WRITE 0x37 ← 0          # unlock EEPROM
WRITE 0x1F ← <offset>   # write position offset
WRITE 0x37 ← 1          # (optional) re-lock
```

Power-cycle the PDB and confirm every joint sits within ±50 units (±4°) of zero.

---

## Stage 2 — Jetson ROS 2 workspace

### 2.1 System packages

```bash
sudo apt update
sudo apt install -y build-essential git \
    python3-colcon-common-extensions python3-rosdep python3-vcstool
sudo rosdep init          # first time only
rosdep update
```

### 2.2 External drivers that are *not* vendored here

`lidar_base.launch.py` includes `cyglidar_d1_ros2`, which is not part of this
repository. Clone it into the workspace before building:

```bash
cd robot_ws/src/vendor
git clone https://github.com/CygLiDAR-ROS/cyglidar_d1_ros2.git
cd ../../..
```

Python-side runtime dependencies (OAK camera, BLE commands, inference):

```bash
pip install depthai bleak onnxruntime numpy opencv-python
# TensorRT + pycuda come from JetPack on the Jetson.
```

### 2.3 Build

```bash
cd robot_ws
rosdep install --from-paths src --ignore-src -r -y
colcon build --symlink-install
source install/setup.bash
```

Add the `source` line to `~/.bashrc` so every new terminal in Stage 3 has it.

> **Message changes first.** If you touch anything under
> `interfaces/robot_interfaces`, rebuild that package before its dependants:
> `colcon build --packages-select robot_interfaces --symlink-install`.

### 2.4 Point the bridge at the right serial device

`robot_ws/src/control/actuator_bridge/config/uart_bridge.param.yaml` defaults to
the Jetson GPIO-header UART:

```yaml
uart_device: "/dev/ttyTHS1"     # USB-UART would be /dev/ttyUSB0
uart_baudrate: 921600
control_rate_hz: 50.0
target_topic: "/control/selected/joint_target"
```

An SPI variant (`spi_bridge.param.yaml`, `/dev/spidev0.0`) exists for the
SPI wiring. Pick one; they are alternative transports to the same STM32.

---

## Stage 3 — Robot bring-up order

Power the robot on a stand with the legs clear of the ground for the first run.
Each block below is a separate terminal, sourced with
`source ~/spot-get-it/robot_ws/install/setup.bash`.

### 3.1 LiDAR perception

```bash
ros2 launch lidar_perception final_lidar_environment_with_memory.launch.py
```

Brings up the CygLiDAR driver, the `base_link → laser_frame` static TF
(`0.14 0.00 0.05`), preprocessing, and both branches:

```
/scan_3D
  └─► /perception/lidar/points_filtered
        ├─► /perception/lidar/obstacle_clusters
        │     └─► /perception/lidar/obstacle_model          ◄── navigation
        └─► /perception/lidar/local_occupancy_grid
              └─► /perception/lidar/obstacle_memory_grid
                    └─► /perception/lidar/local_occupancy_grid_with_memory
                          └─► /perception/lidar/free_space_model  ◄── navigation
```

The memory-grid nodes need `/localization/pose`. If you run this alone, start
the mock publisher from `spot_localization` first.

### 3.2 Localization

```bash
ros2 launch spot_localization final_localization.launch.py
```

Runs `gait_odom_estimator_node`, `mission_tf_node`, `localization_pose_node`.
It expects `/localization/spot_motion` (`robot_interfaces/msg/StmMotion`) from
the actuator bridge, and produces `/localization/odometry`, the
`odom → base_link` TF, the static `mission_map → odom` TF, and
`/localization/pose`.

No STM32 attached? Substitute the mock:

```bash
ros2 launch spot_localization stm_motion_mock.launch.py
```

### 3.3 Navigation

```bash
ros2 launch spot_navigation final_local_path_pipeline.launch.py
```

```
/planning/mock_global_path/spot_01
  └─► path_progress_tracker_node ─► /navigation/path_progress/spot_01
        └─► local_path_planner_node ─► /navigation/local_path/spot_01
                                     ─► /navigation/local_planner_status/spot_01
```

`local_path_planner_node` additionally requires `obstacle_model`,
`free_space_model` and `/localization/pose` — i.e. 3.1 and 3.2 must be up.

### 3.4 Camera and person detection

```bash
ros2 launch robot_bringup camera.launch.py
```

`oak_camera_driver` publishes `/vendor/camera/image_raw` (5 fps) for perception
and pushes MJPEG straight to `rpi_ip:9000`. `camera_perception` runs YOLOv11 —
TensorRT when `engine_path` ends in `.engine`, ONNX Runtime when it ends in
`.onnx` — and publishes `/perception/person_detected/spot_01` only on state
*transitions*, after N consecutive detections.

### 3.5 Control — RL policy, motions and the actuator bridge

```bash
ros2 launch robot_bringup rl_motion_manager.launch.py
```

This is the one that moves the robot. It starts, in one shot:

| Node | Package | Role |
|---|---|---|
| `rl_locomotion_node` | `rl_locomotion` | ONNX policy @ 50 Hz |
| `stand_motion_node` | `motion_manager` | stand-up motion |
| `sit_motion_node` | `motion_manager` | sit motion |
| `detect_motion_node` | `motion_manager` | detection pose |
| `classic_control_node` | `classic_control` | IK + gait pattern |
| `joint_target_mux_node` | `motion_manager` | selects the active source |
| `actuator_bridge_node` | `actuator_bridge` | UART → STM32 @ 50 Hz |

The policy is chosen by config, defaulting to `policy_v6_1_3.yaml`
(`models/spotmicro_v6_1_3_model_4100.onnx`). Swap it on the command line:

```bash
ros2 launch robot_bringup rl_motion_manager.launch.py \
  policy_config:=$(ros2 pkg prefix rl_locomotion)/share/rl_locomotion/config/policy_v7_1.yaml
```

Available profiles: `policy_v5_4_4`, `policy_v6_1_3`, `policy_v6_1_5`,
`policy_v6.3.1`, `policy_v7_1`, `policy_exp043`, `policy_classic_ik_flat`.

The action pipeline is `target_rad = ik_ref + raw_action × action_scale`
(`action_scale: 0.25`). Command limits in `policy_v6_1_3.yaml` are deliberately
conservative — `vx ∈ [-0.03, 0.15] m/s`, `wz ∈ [-0.20, 0.20] rad/s`,
`gait_period 1.2 s`, `body_height 0.170 m`.

### 3.6 Network uplink to the ground station

```bash
ros2 launch network_bringup network_bringup.launch.py rpi5_ip:=192.168.0.13
```

Starts `pose_path_event_sender`, `lidar_stream_sender` and `cmd_receiver_node`.
Set `rpi5_ip` to your Pi. Either stream can be disabled:
`enable_lidar:=false`, `enable_pose_path_event:=false`.

### 3.7 Dry run without any hardware

Validate the whole RL path — ONNX load, observation build, 50 Hz loop — with
fake joint/IMU feedback and no STM32 connected:

```bash
ros2 launch rl_locomotion rl_locomotion.launch.py

ros2 topic pub --once /control/cmd_vel/spot_01 geometry_msgs/msg/Twist \
  "{linear: {x: 0.05, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.0}}"

ros2 topic echo /control/rl/debug --once
```

Expect `cmd_vel` reflected at observation indices 6–8, a 12-element
`raw_action`, and a low `feedback_age_ms`.

### 3.8 Data collection (optional)

```bash
ros2 launch robot_bringup sensor_experimenter.launch.py \
  robot_id:=spot_01 output_root:=~/data session_index:=0
```

`session_index:=0` auto-picks the next free numeric session folder under
`output_root`; video and LiDAR JSONL land in `video/` and `lidar/` beneath it.

---

## Stage 4 — Raspberry Pi 5 ground station

**The daemon must start before the dashboard** — it is what creates the shared
memory the Qt app attaches to.

### 4.1 Bridge daemon

```bash
cd rpi/bridge_app
make
./bridge_daemon 4          # argument = number of robots, default 1
```

Creates `/dev/shm/robot_bridge_0` … `/dev/shm/robot_bridge_{N-1}`. The cap is
`MAX_ROBOTS` in `shm_def.h`. Sanitizer builds: `make asan`, `make tsan`.

Realtime scheduling is optional — without the privilege the daemon retries with
ordinary threads.

### 4.2 Qt dashboard

The CMake target includes headers from `../bridge_app`, so the two directories
must stay siblings.

```bash
cd rpi/qt_app
cmake -S . -B build
cmake --build build
./build/DisasterControlQt
```

Needs Qt 6.5+ with `Core`, `Widgets`, `OpenGL`, `OpenGLWidgets`, and CMake 3.19+.

### 4.3 Data path

```
Jetson ──UDP 9000──► bridge_daemon ──SHM /robot_bridge_N──► Qt dashboard
Jetson ◄─UDP 9001─── bridge_daemon ◄──SHM cmd_queue──────── Qt dashboard
                     bridge_daemon ◄─UDP 9002─► external PC
```

Packet types in `proto.h`: `PKT_TYPE_IMAGE`, `LIDAR`, `ODOM`, `CMD`, `CMD_ACK`,
`STATE`, `EVENT`, `GLOBAL_PATH`, `PATH_PROGRESS`. Image and LiDAR payloads
arrive fragmented and are reassembled by the daemon before hitting shared
memory (triple-buffered).

> `shm_def.h` and `proto.h` are shared by both programs. Change either and you
> must rebuild **both** the daemon and the dashboard.

---

## Stage 5 — RL locomotion training

Runs off-robot on an NVIDIA GPU, outside colcon. `ai_training/rl/` vendors
`legged_gym` and `rsl_rl` with a `spotmicro_test` task.

### 5.1 One-command experiment

```bash
cd ai_training/rl
./run_experiment.sh --purpose "Step 4: add torque penalty"
```

Trains, snapshots mid-run, runs diagnostics, and writes a report. Flags:

| Flag | Effect |
|---|---|
| `--task <name>` | task name (default `spotmicro_test`) |
| `--skip-train` | report only, reuse an existing run |
| `--only-train` | train, skip the report |
| `--skip-visual` | numbers only, no visual check |
| `--train-args "…"` | pass extra args through to `legged_gym` |
| `--no-git` / `--no-push` | don't commit / don't push results |

`LEGGED_GYM_DIR` overrides the `legged_gym` root (default `./legged_gym`).

Each run lands in `ai_training/rl/experiments/<expNNN_name>/` with
`*_metrics.json`, `*_report.md`, TensorBoard curves, per-reward plots, joint
detail and an action-smoothness plot. Roughly 100 such experiments — v1.1
through v8.2 — are kept as the tuning record.

### 5.2 Export and deploy

Export the trained checkpoint to ONNX into `ai_training/rl/exports/`, then copy
it where the node loads it from:

```bash
cp ai_training/rl/exports/spotmicro_v7_1_model.onnx \
   robot_ws/src/control/rl_locomotion/models/
```

Add a matching `robot_ws/src/control/rl_locomotion/config/policy_<ver>.yaml`
(copy an existing one; `model_path` is relative to the package's `models/`),
rebuild `rl_locomotion`, and select it with `policy_config:=` as in §3.5.
`exports/*_vectors/` hold reference I/O vectors for checking that the on-robot
runtime reproduces the training-time outputs.

---

## Stage 6 — Vision model training

```
ai_training/vision/dataset_pipeline/
  01_delete_orphan.py          drop images without labels
  02_make_all_0_person.py      collapse classes to a single "person" class
  03_draw_bbox.py              visual QA of boxes
  03_01_draw_segment.py        visual QA of masks
  04_delete_unfit.py           drop unusable frames
  05_coco_to_yolov11.py        COCO → YOLOv11 layout
  06_segmentation_to_bbox.py   masks → boxes
  07_crawl_from_coco.py        pull extra person images from COCO
  08_filt_coco.py              filter the crawled set
```

Run them in numeric order, then train with
`ai_training/vision/training/person_detection_yolo11.ipynb` (Ultralytics).
Convert the resulting `best.pt` to a TensorRT engine **on the Jetson itself**
(engines are not portable across devices), place it at the path
`camera_perception` expects, and set the parameter if you moved it:

```yaml
engine_path: /home/jetson/models/best.engine   # .engine → TensorRT, .onnx → ONNX Runtime
conf_threshold: 0.7
iou_threshold: 0.45
infer_size: 480
```

---

## Stage 7 — Isaac Sim digital twin

Requires Isaac Sim 5.1.0, ROS 2 Humble, and Isaac's bundled Python 3.11 — which
means a **separate** workspace (`robot_ws_311`) built against that interpreter,
not the Humble workspace from Stage 2.

```bash
cd robot_ws_311
rosdep install --from-paths src --ignore-src -r -y
colcon build --symlink-install
source install/setup.bash

ros2 launch global_path_manager global_path_manager.launch.py
ros2 launch sim_executor sim_executor.launch.py

./d_twin/isaac_projects/launch_isaac.sh
```

Press **Play** in Isaac Sim and answer **Yes** to the ScriptNode warning. The
script nodes under `d_twin/scripts/` subscribe to the robot state and drive the
Spot prim. Two things to keep aligned: the message definitions used by
`d_twin/scripts/` must match the `robot_interfaces` build, and the Script Node
paths inside the scene must point at your `d_twin/scripts/` location. Editing a
script means reloading the scene.

---

## Stage 8 — URDF viewer (no hardware)

The quickest way to confirm a working ROS 2 install and see the kinematics. It
needs no robot, no Jetson and no Isaac.

```bash
sudo apt install -y ros-humble-robot-state-publisher \
                    ros-humble-joint-state-publisher-gui ros-humble-rviz2
source /opt/ros/humble/setup.bash
cd spot-get-it
ros2 launch launch/view_spot.launch.py
```

Starts `robot_state_publisher`, `joint_state_publisher_gui` and `rviz2`. In
RViz set **Fixed Frame** to `base_link` and add a **RobotModel** display with
its topic set to `/robot_description`; then drag the 12 joint sliders.

`urdf/spot_micro_view.urdf` stores mesh URIs as `file://@REPO_ROOT@/…`. The
launch file substitutes the checkout path at load time, so the URDF is portable.
If you need a concrete URDF for some other tool:

```bash
sed "s|@REPO_ROOT@|$PWD|g" urdf/spot_micro_view.urdf > /tmp/spot_micro.urdf
```

Set `SPOT_REPO_ROOT` if you copy the launch file out of the repo.

---

## Topic map

| Topic | Type | Producer → Consumer |
|---|---|---|
| `/scan_3D` | `sensor_msgs/PointCloud2` | CygLiDAR → preprocess |
| `/perception/lidar/points_filtered` | `PointCloud2` | preprocess → clustering, occupancy |
| `/perception/lidar/obstacle_clusters` | `ObstacleClusters` | clustering → obstacle model |
| `/perception/lidar/obstacle_model` | `ObstacleModel` | perception → planner |
| `/perception/lidar/free_space_model` | `FreeSpaceModel` | perception → planner |
| `/vendor/camera/image_raw` | `sensor_msgs/Image` | OAK driver → camera_perception |
| `/perception/person_detected/spot_01` | `std_msgs/Bool` | camera_perception → mission logic |
| `/localization/spot_motion` | `StmMotion` | actuator_bridge → gait odometry |
| `/localization/odometry` | `nav_msgs/Odometry` | gait odometry |
| `/localization/pose` | `LocalizedRobotPose` | localization → perception memory, planner |
| `/planning/mock_global_path/spot_01` | `GlobalPathWaypoints` | global path → tracker |
| `/navigation/path_progress/spot_01` | `PathProgress` | tracker → planner |
| `/navigation/local_path/spot_01` | `nav_msgs/Path` | planner → motion |
| `/navigation/local_planner_status/spot_01` | `LocalPlannerStatus` | planner |
| `/control/cmd_vel/spot_01` | `geometry_msgs/Twist` | commands → RL policy |
| `/control/selected/joint_target` | `JointTarget` | target mux → actuator_bridge |
| `/control/rl/debug` | `RlDebug` | RL policy (diagnostics) |

All 16 message definitions live in
`robot_ws/src/interfaces/robot_interfaces/msg/`.

---

## Verification with RViz

Eight saved configs in `rviz/`, one per pipeline stage — open the one matching
what you just launched:

```bash
rviz2 -d rviz/verification_lidar_preprocess.rviz
```

| Config | Checks |
|---|---|
| `verification_lidar_preprocess` | filtered point cloud |
| `verification_lidar_obstacle_cluster` | clustering |
| `verification_lidar_local_occupancy_grid` | occupancy grid |
| `verification_lidar_free_space_model` | free-space model |
| `verification_spot_localization_base_to_odom` | `odom → base_link` TF |
| `verification_spot_localization_base_to_odom_to_mission` | full TF chain |
| `verification_nav_debug` | path progress, local path |
| `verification_final_perception_localization_navigation` | everything at once |

---

## Troubleshooting

**Servo zero points reset after a power cycle.** STS3215 `EEPROM Lock` (`0x37`)
was left at 1, so the `Position Offset` write only reached RAM. Unlock, rewrite,
re-lock — see §1.3.

**`rosdep` fails.** Re-run `rosdep update` and check the `package.xml`
dependency names. Rebuild a single package with
`colcon build --packages-select <pkg> --symlink-install`.

**Stale environment after a build.** Source `install/setup.bash` in a *fresh*
terminal; a shell that sourced the old install tree keeps stale paths.

**Qt dashboard won't connect.** Confirm `/dev/shm/robot_bridge_N` exists — i.e.
the daemon is actually running and was started with enough robots.

**Commands don't reach the robot.** The daemon learns the Jetson's UDP source
address from incoming telemetry; it cannot send until the Jetson has sent
something first.

**Struct or protocol change didn't take effect.** Rebuild both the daemon and
the dashboard; they share `shm_def.h` and `proto.h`.

**BNO055 I²C bus hangs.** See
[`firmware/bno055_i2c_busy_recovery_report.md`](firmware/bno055_i2c_busy_recovery_report.md).

**`cyglidar_d1_ros2` not found.** It is an external driver — clone it into
`robot_ws/src/vendor/` (§2.2).

**These launch files are empty placeholders** and will do nothing if you call
them: `robot_bringup/launch/control_core.launch.py`,
`hardware_stand.launch.py`, `hardware_rl_low_speed.launch.py`. Use
`rl_motion_manager.launch.py` instead.

---

## What is not in this repo

Excluded to keep the clone small; regenerate or fetch from upstream as needed.

| Excluded | Size | Where to get it |
|---|---|---|
| Upstream git history and branches | 864 MB | [upstream repo](https://github.com/jeayoungho97/SPOT_GET_IT) |
| `*.pt` RL checkpoints (~96 of them) | 420 MB | retrain via `run_experiment.sh`; the deployed `.onnx` exports **are** tracked |
| `hardware/cad/` — `.f3z`, `.step`, `.stl` masters | 184 MB | upstream Git LFS |
| `*.blend` mesh sources | 30 MB | upstream; the `.stl`/`.dae` the sim loads **are** tracked |

Still tracked: every source file, all ONNX policies, the sim meshes, every
experiment report and plot, the RViz configs, and the hardware documentation.

---

## Credits

Original project and authors — 제영호 (jeayoungho97), 한승현 (tmdgus1dnl),
방인규 (Swanut97), 배경근 (bae-g-g), 김병우 (bw522), 구진영 (koo001602).
Upstream: <https://github.com/jeayoungho97/SPOT_GET_IT>.

Licensed under the MIT License — see [`LICENSE`](LICENSE). Copyright (c) 2026
jeayoungho97.
