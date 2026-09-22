# classic_control integration notes

이 패키지는 현재 `robot_ws`의 기존 인터페이스를 변경하지 않는 방식으로
통합한다.

## 현재 워크스페이스 계약

- `motion_manager/joint_target_mux_node`는 `STAND`, `RL`, `CLASSIC`
  behavior mode를 허용한다.
- `joint_target_mux_node`는 `/control/stand/joint_target`,
  `/control/rl/joint_target`, `/control/classic_control/joint_target` 중
  하나를 `/control/selected/joint_target`로 내보낸다.
- `robot_interfaces/msg/JointTarget`에는 `MODE_CLASSIC` /
  `MODE_CLASSIC_CONTROL` enum이 있다. `motion_state` 필드는 없다.
- `actuator_bridge_node`가 `/control/cmd_vel/spot_01`을 구독해 STM command
  packet의 `motion_state`를 만들고, STM feedback에서
  `/localization/spot_motion`을 publish한다.

## classic_control 쪽 정합 방식

- `classic_control_node`는 기본 target topic을
  `/control/classic_control/joint_target`로 사용한다.
- publish하는 `JointTarget.mode`는 `MODE_CLASSIC` 값을 사용한다.
- classic target은 `locomotion_common/SharedTrotReference`에서 만든다.
  RL은 `ik_profile: shared_v1`로 같은 reference를 사용할 수 있으며, 새 RL
  학습은 이 profile 기준으로 진행하는 것을 권장한다.
- `/control/behavior/mode`에 `CLASSIC`을 publish하면 motion manager가 classic
  target을 선택한다.
- `JointTarget.motion_state`는 쓰지 않는다. 고전 제어에서 motion state가
  필요해지는 경우에는 `actuator_bridge_node`가 publish하는
  `/localization/spot_motion`을 구독해야 한다.

## 추가 분리가 필요할 때

ROS message level에서도 classic target을 RL target과 구분하려면 아래 기존
패키지의 계약 변경이 추가로 필요하다. 현재 변경에는 포함하지 않았다.

- `robot_interfaces/msg/JointTarget`: classic semantic mode 추가 여부 결정
- `actuator_bridge_node`: classic semantic mode를 STM wire `OPERATE`로 매핑
