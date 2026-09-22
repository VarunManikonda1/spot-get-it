# 실험 089: spotmicro_v7_1_slope_terrain_gentle_restart

- **날짜:** 2026-05-26 14:29
- **Task:** spotmicro_test
- **Run name:** `spotmicro_v7_1_slope_terrain_gentle_restart`
- **판정:** ❌ FAIL (일부 기준 미달)

---

## 실험 목적

v7.1: 지형 학습 설정 변경 후 학습 - walk-eval

---

## 변경점 (이전 커밋 대비)

```diff
(변경 사항 없음 또는 git 미설정)
```

**변경 요약:**
(변경 사항 없음 또는 git 미설정)

---

## 현재 Reward Scales

| Reward | Scale |
|--------|-------|
| action_rate | -0.05 |
| ang_vel_xy | -1.0 |
| ang_vel_xy_recovery | 0.5 |
| base_height | -2.0 |
| collision | -1.0 |
| dof_acc | -2.5e-07 |
| dof_vel | -0.0005 |
| feet_air_time | 0.04 |
| feet_clearance | 0.03 |
| lin_vel_z | -2.0 |
| no_stuck_feet | -0.2 |
| orientation | -10.0 |
| stand_still | -0.4 |
| swing_contact | -0.45 |
| termination | -20.0 |
| tilt_recovery | 4.0 |
| torques | -0.001 |
| tracking_ang_vel | 0.6 |
| tracking_ik | 0.6 |
| tracking_lin_vel | 1.0 |
| trot_contact | 0.35 |

---

## 진단 결과 (Diagnostic)


### 지형 설정

| 항목 | 값 |
|------|-----|
| 평가 모드 | terrain_random_walk |
| walk_eval | True |
| mesh_type | trimesh |
| terrain_profile | spotmicro_slope |
| measure_heights | False |
| grid | 5 x 5 |
| env 크기 | 6.0 x 6.0 m |
| terrain_proportions | [0.5, 0.25, 0.25] |
| slope max | 0.1 |
| rolling amp max | 0.015 m |


### 핵심 지표

- ⚠️ Timeout: 76.2% (60~80%, 개선 필요)
- ✅ 속도오차 X: 0.0219 m/s (<0.08)
- ⚠️ 토크포화: 13.2% (10~40%)
- ✅ 자세: roll 1.4°, pitch 1.7° (<10°)
- ✅ 조기종료: 0.0% (<5%)
- ⚠️ 18도+ pre-fall 복구: trial 없음 (30도 목표 미검증)
- ℹ️ Recovery/pre-fall 지표는 참고값입니다 (terrain run 자동 PASS/FAIL 기준에서는 제외)

### 상세 수치

| 지표 | 값 |
|------|-----|
| Timeout% | 76.2214983713355 |
| 조기종료% | 0.0 |
| 속도오차 X | 0.021869441494345665 m/s |
| 속도오차 Y | 0.01200190745294094 m/s |
| 각속도오차 | 0.07411336898803711 rad/s |
| 토크포화% | 13.231603465978464 |
| 평균 높이 | 0.1746739864279896 m |
| Roll (평균) | 1.4144283533096313° |
| Pitch (평균) | 1.7491778135299683° |
| Action Rate | 0.014352523721754551 |
| 평균 전력 | 4.320265769958496 W |
| CoT | 2.967671328274072 |
| Recovery 성공률 | None% |
| Recovery eligible trials | 0 |
| 평균 회복 시간 | None s |
| Recovery 조기 실패율 | None% |
| Recovery 18도+ 성공률 | None% |
| Recovery 18도+ trials | 0 |
| Recovery 18도+ horizon 후 tilt | None° |
| Transition recovery 성공률 | None% |
| Transition recovery eligible trials | 0 |
| 평균 Transition recovery 시간 | None s |
| Transition 18도+ 성공률 | None% |
| Transition 18도+ trials | 0 |
| Transition 18도+ horizon 후 tilt | None° |

## Recovery Assist 분석

| 지표 | 값 |
|------|-----|
| 판정 | ⚠️ eligible trial 없음 |
| Recovery 성공률 | N/A |
| 성공/실패 | 0 / 0 |
| Eligible trials | 0 / 865 |
| 평균 회복 시간 | N/A |
| 조기 실패율 | N/A |
| 평가 horizon | 1.0 s |
| 초기 tilt 기준 | 12.000000000000002° |
| 안정 기준 | roll/pitch < 5.0°, height > 0.165m |
| 평균 초기 tilt | None° |
| 평균 초기 roll/pitch | None° / None° |
| 1초 후 평균 roll/pitch | None° / None° |
| 1초 내 최대 roll/pitch 평균 | None° / None° |

> 해석: `Recovery 성공률`은 초기 tilt가 기준 이상인 episode 중 1초 내 안정 자세로 복귀한 비율입니다. 조기 실패율이 높으면 reset 직후 바로 넘어지는 것이고, 평균 회복 시간이 짧을수록 위기 대응이 빠른 것입니다.

### Recovery Tilt Band 분석

| Tilt 구간 | Trials | 성공률 | Horizon 후 평균 tilt | 평균 회복 시간 |
|-----------|--------|--------|----------------------|----------------|
| 12-18 deg | 0 | N/A | N/A | N/A |
| 18-25 deg | 0 | N/A | N/A | N/A |
| 25-30 deg | 0 | N/A | N/A | N/A |
| 30+ deg | 0 | N/A | N/A | N/A |
| 18+ deg 전체 | 0 | N/A | N/A | N/A |

> 이번 pre-fall 목표는 특히 `18-25 deg`, `25-30 deg`, `18+ deg 전체` 행을 우선 봅니다. `25-30 deg` trial이 없으면 30도 근처 회복 성능은 아직 검증되지 않은 것입니다.



## Transition Recovery 분석

| 지표 | 값 |
|------|-----|
| 판정 | ⚠️ eligible trial 없음 |
| Transition recovery 성공률 | N/A |
| 성공/실패 | 0 / 0 |
| Eligible trials | 0 / 0 |
| 평균 회복 시간 | N/A |
| 조기 실패율 | N/A |
| 평가 horizon | 0.75 s |
| push 후 eligible 기준 | max roll/pitch > 12.000000000000002° |
| 안정 기준 | roll/pitch < 5.0°, height > 0.165m |
| 평균 최대 tilt | None° |
| horizon 후 평균 roll/pitch | None° / None° |

> 해석: `Transition Recovery`는 학습 중 push/roll-pitch angular impulse가 들어간 뒤 일정 시간 안에 자세가 안정 기준으로 돌아오는지를 봅니다.

### Transition Recovery Tilt Band 분석

| Tilt 구간 | Trials | 성공률 | Horizon 후 평균 tilt | 평균 회복 시간 |
|-----------|--------|--------|----------------------|----------------|
| 12-18 deg | 0 | N/A | N/A | N/A |
| 18-25 deg | 0 | N/A | N/A | N/A |
| 25-30 deg | 0 | N/A | N/A | N/A |
| 30+ deg | 0 | N/A | N/A | N/A |
| 18+ deg 전체 | 0 | N/A | N/A | N/A |

> 이번 pre-fall 목표는 특히 `18-25 deg`, `25-30 deg`, `18+ deg 전체` 행을 우선 봅니다. `25-30 deg` trial이 없으면 30도 근처 회복 성능은 아직 검증되지 않은 것입니다.



## Command Mode별 회전 추종 분석

| 모드 | 비율 | 샘플 수 | 평균 abs(cmd_wz) | 평균 abs(actual_wz) | yaw 오차 | 상태 |
|------|------|---------|------------------|---------------------|----------|------|
| 정지 | 12.3% | 94235 | 0.0268 | 0.0700 | 0.0675 | ❌ |
| 직진/저회전 | 41.3% | 317134 | 0.0774 | 0.1029 | 0.0772 | ✅ |
| 제자리 회전 | 9.9% | 76301 | 0.1741 | 0.1450 | 0.0734 | ✅ |
| 전진+회전 | 13.2% | 101385 | 0.1771 | 0.1661 | 0.0795 | ✅ |
| 큰 회전명령 | 0.0% | 0 | N/A | N/A | N/A | N/A |

> 해석 기준: `제자리 회전`만 나쁘면 pure turn 학습/보행 패턴 문제, `큰 회전명령`만 나쁘면 yaw 명령 범위가 현재 토크/보폭 한계보다 큰 문제, `정지`가 나쁘면 stop drift 문제로 보면 됩니다.
        


## 학습 곡선 분석

| 지표 | 값 | 판정 |
|------|-----|------|
| 수렴 iter (90%) | 8142 | 느림 |
| 후반 안정성 (CV) | 0.027 | 안정 |
| 정체 구간 | 없음 | |


## Per-leg 분석

### 발별 접촉/공중 비율

| 발 | 접촉% | 공중% |
|-----|-------|------|
| foot_0 | 79.3% | 20.7% |
| foot_1 | 75.4% | 24.6% |
| foot_2 | 79.9% | 20.1% |
| foot_3 | 67.6% | 32.4% |

### 관절별 토크 포화

| 관절 | 포화% | 최대토크 | 한계 | 상태 |
|------|------|---------|------|------|
| front_left_shoulder | 0.0% | 2.940 | 2.940 | ✅ |
| front_left_leg | 0.2% | 2.940 | 2.940 | ✅ |
| front_left_foot | 28.0% | 2.940 | 2.940 | ❌ |
| front_right_shoulder | 0.0% | 2.940 | 2.940 | ✅ |
| front_right_leg | 5.5% | 2.940 | 2.940 | ⚠️ |
| front_right_foot | 17.8% | 2.940 | 2.940 | ⚠️ |
| rear_left_shoulder | 1.4% | 2.940 | 2.940 | ✅ |
| rear_left_leg | 49.6% | 2.940 | 2.940 | ❌ |
| rear_left_foot | 33.4% | 2.940 | 2.940 | ❌ |
| rear_right_shoulder | 0.0% | 2.940 | 2.940 | ✅ |
| rear_right_leg | 5.5% | 2.940 | 2.940 | ⚠️ |
| rear_right_foot | 17.2% | 2.940 | 2.940 | ⚠️ |


## Gait 분석

| 지표 | 값 |
|------|-----|
| Gait 주파수 | 0.21 Hz |
| Gait 주기 | 58 steps |
| 대각 동기화율 | 86.6% |
| L/R 비대칭 | 0.0%p |
| 판정 | ✅ Trot 패턴 / ✅ 대칭 |


## 관절별 상세

| 관절 | 토크포화% | 사용범위% | 편향(rad) | 한계근접% | 상태 |
|------|----------|----------|----------|----------|------|
| front_left_shoulder | 0.0% | 80% | -0.066 | 0.0% | ✅ |
| front_left_leg | 0.2% | 15% | -1.004 | 0.0% | ⚠️ |
| front_left_foot | 28.0% | 31% | +1.747 | 0.0% | ⚠️ |
| front_right_shoulder | 0.0% | 58% | +0.028 | 0.0% | ✅ |
| front_right_leg | 5.5% | 19% | -1.097 | 0.0% | ⚠️ |
| front_right_foot | 17.8% | 27% | +1.744 | 0.0% | ⚠️ |
| rear_left_shoulder | 1.4% | 59% | +0.011 | 0.0% | ✅ |
| rear_left_leg | 49.6% | 19% | -1.189 | 0.0% | ⚠️ |
| rear_left_foot | 33.4% | 25% | +1.722 | 0.0% | ⚠️ |
| rear_right_shoulder | 0.0% | 44% | +0.065 | 0.0% | ✅ |
| rear_right_leg | 5.5% | 16% | -1.104 | 0.0% | ⚠️ |
| rear_right_foot | 17.2% | 27% | +1.626 | 0.0% | ⚠️ |


## 에너지 효율 상세

| 지표 | 값 |
|------|-----|
| 평균 전력 | 4.32 W |
| 피크 전력 | 43.84 W |
| 피크/평균 비율 | 10.1x |
| CoT | 2.97 |

### 관절별 전력 분배

| 관절 | 평균전력(W) | 비율% |
|------|-----------|-------|
| front_left_foot | 0.790 | 18.3% |
| rear_left_foot | 0.660 | 15.3% |
| front_right_foot | 0.588 | 13.6% |
| rear_right_foot | 0.502 | 11.6% |
| rear_left_leg | 0.472 | 10.9% |
| front_right_leg | 0.395 | 9.1% |
| rear_right_leg | 0.330 | 7.6% |
| front_left_leg | 0.259 | 6.0% |
| rear_left_shoulder | 0.147 | 3.4% |
| front_right_shoulder | 0.088 | 2.0% |
| rear_right_shoulder | 0.048 | 1.1% |
| front_left_shoulder | 0.040 | 0.9% |


## 이전 실험 대비 비교

| 지표 | 이전 (exp088) | 현재 (exp089) | 변화 |
|------|-------|-------|------|
| Timeout% | 65.0% | 76.2% | ✅ ↑ 11.2073% |
| 속도오차 X | 0.0287 | 0.0219 | ✅ ↓ 0.0068m/s |
| 토크포화 | 13.1% | 13.2% | ⚠️ ↑ 0.1405% |
| Roll | 2.0° | 1.4° | ✅ ↓ 0.6087° |
| Pitch | 2.3° | 1.7° | ✅ ↓ 0.5841° |
| 평균 전력 | 4.4956W | 4.3203W | ✅ ↓ 0.1753W |


## Tensorboard 학습 곡선 요약

| 스칼라 | 최종값 | 최대 | 최소 | 후반10% 평균 |
|--------|--------|------|------|-------------|
| rew_action_rate | -0.0457 | -0.0005 | -0.0460 | -0.0440 |
| rew_ang_vel_xy | -0.0995 | -0.0254 | -0.1035 | -0.0944 |
| rew_ang_vel_xy_recovery | 0.0007 | 0.0017 | 0.0004 | 0.0006 |
| rew_base_height | -0.0003 | -0.0000 | -0.0005 | -0.0003 |
| rew_collision | -0.0005 | 0.0000 | -0.0124 | -0.0002 |
| rew_dof_acc | -0.0067 | -0.0003 | -0.0069 | -0.0065 |
| rew_dof_vel | -0.0034 | -0.0002 | -0.0035 | -0.0033 |
| rew_feet_air_time | -0.0001 | 0.0000 | -0.0002 | -0.0001 |
| rew_feet_clearance | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| rew_lin_vel_z | -0.0047 | -0.0012 | -0.0054 | -0.0049 |
| rew_no_stuck_feet | -0.0036 | -0.0000 | -0.0044 | -0.0038 |
| rew_orientation | -0.0333 | -0.0213 | -0.0650 | -0.0336 |
| rew_stand_still | -0.0241 | -0.0081 | -0.0600 | -0.0253 |
| rew_swing_contact | -0.0663 | -0.0002 | -0.0721 | -0.0667 |
| rew_termination | -0.0032 | -0.0009 | -0.0144 | -0.0040 |
| rew_tilt_recovery | 0.0007 | 0.0009 | 0.0003 | 0.0007 |
| rew_torques | -0.0268 | -0.0003 | -0.0268 | -0.0253 |
| rew_tracking_ang_vel | 0.3439 | 0.4380 | 0.0020 | 0.3389 |
| rew_tracking_ik | 0.3150 | 0.4105 | 0.0020 | 0.3219 |
| rew_tracking_lin_vel | 0.8710 | 0.9228 | 0.0041 | 0.8637 |
| rew_trot_contact | 0.2556 | 0.2794 | 0.0004 | 0.2516 |
| terrain_level | 4.5952 | 4.5952 | 0.4184 | 4.5671 |
| learning_rate | 0.0003 | 0.0004 | 0.0000 | 0.0003 |
| surrogate | -0.0025 | 0.0008 | -0.0047 | -0.0029 |
| value_function | 0.0144 | 0.2258 | 0.0047 | 0.0185 |
| collection time | 1.7184 | 6.6005 | 1.6034 | 1.6791 |
| learning_time | 0.5779 | 0.6016 | 0.5252 | 0.5858 |
| total_fps | 42809.0000 | 45005.0000 | 13697.0000 | 43407.4750 |
| mean_noise_std | 0.1729 | 0.1729 | 0.0764 | 0.1701 |
| mean_episode_length | 927.7700 | 991.5000 | 22.1200 | 929.5145 |
| time | 927.7700 | 991.5000 | 22.1200 | 929.5145 |
| mean_reward | 30.2722 | 37.4909 | -0.3311 | 30.4884 |
| time | 30.2722 | 37.4909 | -0.3311 | 30.4884 |

총 학습 iteration: 8899


---

## 그래프

### Tensorboard 학습 곡선
![tb_training_curves.png](./tb_training_curves.png)
![tb_individual_rewards.png](./tb_individual_rewards.png)

### Diagnostic 결과
![diagnostic_report.png](./diagnostic_report.png)
![joint_detail.png](./joint_detail.png)
![action_smoothness.png](./action_smoothness.png)
![recovery_report.png](./recovery_report.png)


---

## 결론 및 다음 실험 방향

**자동 판정:** ❌ FAIL (일부 기준 미달)

대부분 기준을 통과했으나 일부 항목이 보통 수준입니다.
  - ⚠️ Timeout: 76.2% (60~80%, 개선 필요)
  - ⚠️ 토크포화: 13.2% (10~40%)
  - ⚠️ 18도+ pre-fall 복구: trial 없음 (30도 목표 미검증)

> 💡 위 결론은 자동 생성되었습니다. 필요시 수정하세요.

---

## 메모

_(추가 메모가 있으면 여기에 작성)_

