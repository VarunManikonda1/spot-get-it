# SpotMicro RL 실험 로그

| # | 날짜 | Run Name | 핵심 변경 | Timeout% | 속도오차X | 토크포화 | 결과 |
|---|------|----------|----------|----------|----------|---------|------|
| 001 | 04/24 | spotmicro_v1_0_ik_tracking | V1.0: default IK 동작을 모방하여 보행, 환경에 따른 변수를... | 100.0% | 0.103 | 16.4% | ✅ |
| 002 | 04/24 | spotmicro_v1_1_0_lin_vel_improve | V1.1.0: 직선속도 추종 정도 향상을 위한 reward 가중치 및 a... | 100.0% | 0.085 | 16.0% | ✅ |
| 003 | 04/24 | spotmicro_v1_1_1_lin_vel_improve | V1.1.1: 직선속도 추종 정도는 유지하며 몸체의 흔들림 감소 | 100.0% | 0.082 | 14.2% | ✅ |
| 004 | 04/24 | spotmicro_v1_1_2_orientation_improve | V1.1.2: 추종을 유지하며 줄 수 있는 페널티 수치 확인 | 100.0% | 0.087 | 12.7% | ✅ |
| 005 | 04/24 | spotmicro_v1_1_3_vel_improve | V1.1.3: 직선속도, 각속도 추종 오차 최소화 | 81.5% | 0.089 | 47.5% | ❌ |
| 006 | 04/27 | spotmicro_v1_1_4_sigma_desc | V1.1.4: tracking sigma 조정으로 민감도를 올림, 속도 ... | 100.0% | 0.060 | 13.6% | ✅ |
| 007 | 04/27 | spotmicro_v2_0_DR_friction | V2.0: 도메인 랜덤화 적용. 우선 지면 마찰계수만 적용 | 100.0% | 0.069 | 12.8% | ✅ |
| 008 | 04/27 | spotmicro_v2_1_DR_mass | V2.0: 질량 랜덤화 적용하여 오차와 안정성을 유지하는지 확인 | 100.0% | 0.063 | 14.8% | ✅ |
| 009 | 04/27 | spotmicro_v2_2_DR_external_push | V2.2: 외부의 충격량 적용. | 100.0% | 0.070 | 14.4% | ✅ |
| 010 | 04/27 | spotmicro_v2_3_DR_sensor_noise | V2.3: 센서 노이즈 값 추가. | 99.2% | 0.077 | 12.9% | ✅ |
| 011 | 04/27 | spotmicro_v2_3_1_DR_sensor_noise | V2.3.1: sigma값 낮춰서 노이즈 환경에서 성능이 좋아지는지 확인... | 100.0% | 0.053 | 14.3% | ✅ |
| 012 | 04/27 | spotmicro_v2_3_2_DR_sensor_noise | V2.3.2: 각속도 추종 reward를 직선속도 추종 reward랑 비... | 100.0% | 0.051 | 20.0% | ✅ |
| 013 | 04/27 | spotmicro_v2_4_DR_servo_delay | V2.4: 실제로 서보모터 응답의 지연을 이전 step의 액션을 적용하는... | 100.0% | 0.061 | 12.9% | ✅ |
| 014 | 04/27 | spotmicro_v2_5_stand_still | V2.5: 속도 범위를 0.0 포함하는 범위로 수정, 가만히 있는 동작 ... | 5.7% | 0.156 | 31.0% | ❌ |
| 015 | 04/27 | spotmicro_v2_5_1_stand_still | V2.5.1: gait 진행을 속도 비례로 수정, 정지 명령 때 trot... | 100.0% | 0.042 | 12.4% | ✅ |
| 016 | 04/28 | spotmicro_v2_5_2_stand_turn | V2.5.2: 제자리에서 회전할 수 있도록 설정 | 100.0% | 0.045 | 15.9% | ✅ |
| 017 | 04/28 | spotmicro_v2_6_reset_state | V2.6: 리셋 상태 랜덤화 | 96.2% | 0.053 | 18.2% | ✅ |
| 018 | 04/28 | spotmicro_v2_7_stab_ang_improve | V2.7: 나빠진 안정성, 각속도 오차 성능 향상 | 97.7% | 0.049 | 16.3% | ✅ |
| 019 | 04/28 | spotmicro_v2_7_1_stab_ang_improve | V2.7.1: 나빠진 안정성, 각속도 오차 성능 향상, iteration... | 100.0% | 0.047 | 18.1% | ✅ |
| 020 | 04/28 | spotmicro_v2_8_ang_shoulder | V2.8: ik shoulder 관절 목표값 설정, 회전 오차 감소하는지... | 5.7% | 0.077 | 22.2% | ❌ |
| 021 | 04/28 | spotmicro_v2_9_angle_weight | V2.9: tracking_ik의 관절 각도 가중치 설정, 어깨 관절에 ... | 98.5% | 0.052 | 28.3% | ✅ |
| 022 | 04/28 | spotmicro_v2_10_ang_improve | V2.10: 가중치 복구, trot contact reward 제자리 회... | 98.5% | 0.047 | 17.6% | ✅ |
| 023 | 04/28 | spotmicro_v2_11_resample_mode | V2.11: 여러 경우의 동작을 mode로 구분하여 학습시킴 | 96.2% | 0.074 | 37.2% | ✅ |
| 024 | 04/28 | spotmicro_v3_0_change_urdf | V3.0: urdf 실제 무게 반영 | 97.7% | 0.053 | 33.8% | ✅ |
| 025 | 04/28 | spotmicro_v3_1_PD_change | V3.1: urdf 변경으로 인한 변경점을 보정하기 위한 PD 변경. | 92.1% | 0.043 | 18.1% | ✅ |
| 026 | 04/29 | spotmicro_v3_1_1_urdf_fix | V3.1.1: urdf inertia 오류 수정. | 98.5% | 0.050 | 20.5% | ✅ |
| 027 | 04/29 | spotmicro_v3_1_2_PD_adjust | V3.1.2: 수정했던 PD를 원복. | 92.1% | 0.050 | 34.3% | ✅ |
| 028 | 04/29 | spotmicro_v3_1_3_PD_adjust | V3.1.3: PD수치 조정을 통해 토크 포화 보정 시도. | 93.4% | 0.048 | 19.0% | ✅ |
| 029 | 04/29 | spotmicro_v3_2_noise_order | V3.2: observation 조정으로 인한 노이즈 함수 순서 조정 처... | 96.2% | 0.046 | 20.3% | ✅ |
| 030 | 04/29 | spotmicro_v3_3_ik_desc | V3.3: IK 구조가 토크를 과도하게 먹는 구조인지 확인하기 위한 IK... | 94.8% | 0.044 | 24.7% | ✅ |
| 031 | 04/29 | spotmicro_v3_3_1_torque_improve | V3.3.1: torque 페널티 증가시켜 leg, foot 토크 완화 ... | 97.0% | 0.047 | 19.2% | ✅ |
| 032 | 04/29 | spotmicro_v3_3_2_torque_improve | V3.3.2: torque  페널티 적당히 조절, leg, foot 토크... | 6.0% | 0.096 | 26.4% | ❌ |
| 033 | 05/04 | spotmicro_v3_4_ik_sigma_asc | V3.4: tracking_ik 의 sigma를 감소한 pd gain 값... | 99.2% | 0.041 | 22.2% | ✅ |
| 034 | 05/04 | spotmicro_v4_0_new_tracking_ik | V4.0: tracking_ik 의 방식을 action에 따른 rewar... | 94.8% | 0.053 | 18.8% | ✅ |
| 035 | 05/04 | spotmicro_v4_0_1_shoulder_weight | V4.0.11: shoulder 관절 weight를 0.9로 변경. 좀 ... | 96.2% | 0.033 | 23.3% | ✅ |
| 036 | 05/04 | spotmicro_v4_1_new_IK | V4.1: IK를 2D에서 quasi-3D로 변경. | 98.5% | 0.032 | 24.7% | ✅ |
| 037 | 05/04 | spotmicro_v4_2_height_reward | V4.2: 높이 reward 추가하여 뒷다리 토크 문제 해결 시도 | 90.8% | 0.032 | 22.4% | ✅ |
| 038 | 05/04 | spotmicro_v4_3_body_height | V4.3: IK의 body height를 낮춰 토크 포화 감소 시도 | 49.6% | 0.039 | 25.0% | ❌ |
| 039 | 05/04 | spotmicro_v4_4_pitch_z | V4.4: pitch/roll 에 따라 leg의 z값 보정, 균형 맞춰서... | 87.1% | 0.037 | 21.3% | ✅ |
| 040 | 05/06 | spotmicro_v4_4_pitch_z | V4.5: IK를 좀 덜 공격적이게 변경, 추종도 낮춰서 토크 포화 해결... | 74.9% | 0.035 | 19.4% | ❌ |
| 041 | 05/06 | spotmicro_v4_6_lin_range_desc | V4.6: 속도 범위 감소시켜 토크에 영향 있는지 확인 | 92.1% | 0.042 | 23.4% | ✅ |
| 042 | 05/11 | spotmicro_v3_2_noise_order | V5: v3.2 버전으로 롤백 | 96.2% | 0.046 | 20.3% | ✅ |
| 043 | 05/11 | spotmicro_v3_2_noise_order | V5: v3.2 버전으로 롤백, 초기 모델 | 97.7% | 0.046 | 19.8% | ✅ |
| 044 | 05/11 | spotmicro_v5_1_terrain_curriculum | V5.1: 복잡한 지형 학습 | 0.0% | 0.146 | 28.0% | ❌ |
| 045 | 05/11 | spotmicro_v5_2_gait_period | V5.2: gait period, feet height 조정 | 81.5% | 0.112 | 6.3% | ✅ |
| 046 | 05/11 | spotmicro_v5_2_1_gait_perioid | V5.2.1: gait period 수치 조정 | 90.7% | 0.078 | 11.0% | ✅ |
| 047 | 05/12 | spotmicro_v5_2_2_tracking_ik_desc | V5.2.2: IK tracking 완화 | 90.7% | 0.078 | 11.0% | ✅ |
| 048 | 05/12 | spotmicro_v5_2_2_tracking_ik_desc | V5.2.2: IK tracking 완화 | 94.8% | 0.049 | 16.8% | ✅ |
| 049 | 05/12 | spotmicro_v5_2_2_tracking_ik_desc | V5.2.1: gait period 조정 | 94.8% | 0.049 | 16.8% | ✅ |
| 050 | 05/12 | spotmicro_v5_2_2_tracking_ik_desc | V5.2.1: gait period 조정 | 94.8% | 0.050 | 14.5% | ✅ |
| 051 | 05/12 | spotmicro_v5_3_stand_still | V5.3: 정지상태 yaw 오차 감소 | 89.5% | 0.046 | 17.4% | ✅ |
| 052 | 05/12 | spotmicro_v5_4_IK_yaw | V5.4: yaw_ik 수정 | 98.5% | 0.049 | 14.2% | ✅ |
| 053 | 05/12 | spotmicro_v5_4_1_shoulder_tuning | V5.4.1: shoulder ref 약화 | 27.3% | 0.060 | 17.4% | ❌ |
| 054 | 05/12 | spotmicro_v5_4_2_stride_tuning | V5.4.2: shoulder reg delete | 92.8% | 0.049 | 14.4% | ✅ |
| 055 | 05/12 | spotmicro_v5_4_3_turn_width | V5.4.3: half turn width parameter 추가 | 27.3% | 0.058 | 17.7% | ❌ |
| 056 | 05/13 | spotmicro_v5_4_4_IK rollback | V5.4.4: IK rollback 후 각 체크포인트마다 확인 | 98.5% | 0.049 | 14.2% | ✅ |
| 057 | 05/13 | spotmicro_v5_4_4_IK rollback | V5.4.4.1: IK rollback 후 각 체크포인트마다 확인 - 7... | 95.5% | 0.048 | 14.6% | ✅ |
| 058 | 05/13 | spotmicro_v5_4_4_IK rollback | V5.4.4.2: IK rollback 후 각 체크포인트마다 확인 - w... | 99.2% | 0.050 | 14.0% | ✅ |
| 059 | 05/15 | spotmicro_v5_6_recovery_assist | V5.6: recovery test | 100.0% | 0.048 | 11.5% | ✅ |
| 060 | 05/15 | spotmicro_v5_6_1_recovery_assist_push | V5.6.1: recovery test - add push | 93.1% | 0.056 | 8.4% | ✅ |
| 061 | 05/19 | spotmicro_v6_0_shared_ik_retrain | v6.0: 새로운 IK로 학습 | 97.5% | 0.025 | 2.1% | ✅ |
| 062 | 05/19 | spotmicro_v6_0_1_shared_ik_cmd_deadband | v6.0.1: deadband 조정으로 걷는지 확인 | 93.4% | 0.053 | 2.0% | ✅ |
| 063 | 05/19 | spotmicro_v6_0_2_forward_tracking | v6.2: 설정 common IK 초기와 동일하게 변경하고 선속도 추종 ... | 92.3% | 0.029 | 2.1% | ✅ |
| 064 | 05/20 | spotmicro_v6_0_3_low_speed_clearance | v6.0.3: 저속 구간 재설정 및 다리 안드는 페널티 추가 | 90.0% | 0.026 | 5.9% | ✅ |
| 065 | 05/20 | spotmicro_v6_0_4_reward_retune | v6.0.4: reward tuning. swing 중 충돌 잘 안하는지... | 92.8% | 0.026 | 9.4% | ✅ |
| 066 | 05/20 | spotmicro_v6_1_recovery_resume | v6.1: exp065에서 resume, recovery 안정성 강화 | 93.9% | 0.024 | 7.9% | ✅ |
| 067 | 05/20 | spotmicro_v6_1_1_contact_termination | v6.1.1: exp065에서 resume, recovery 안정성 강화 | 99.4% | 0.026 | 3.1% | ✅ |
| 068 | 05/20 | spotmicro_v6_1_2_transition_recovery | v6.1.2: roll/pitch recovery reward 추가 | 100.0% | 0.020 | 2.6% | ✅ |
| 069 | 05/20 | spotmicro_v6_1_3_stronger_transition_push | v6.1.3: push, roll 수치 강화 | 99.8% | 0.020 | 2.7% | ✅ |
| 070 | 05/20 | spotmicro_v6_1_4_moderate_shove_recovery | v6.1.4: recovery 수치 강화 | 99.8% | 0.020 | 3.1% | ✅ |
| 071 | 05/23 | spotmicro_v6_1_5_slippery_rear_com_motor_dr | v6.1.5: 랜덤화 추가 및 마찰 감소 | 100.0% | 0.030 | 2.1% | ✅ |
| 073 | 05/25 | spotmicro_v6_2_prefall_tilt_recovery | v6.2 : tilt 회복 | 99.4% | 0.024 | 3.6% | ❌ |
| 074 | 05/25 | spotmicro_v6_2_1_prefall_tilt_recovery_25deg | v6.2.1 : tilt 회복 각도 확장 | 97.0% | 0.022 | 4.1% | ❌ |
| 075 | 05/25 | spotmicro_v6_2_2_prefall_tilt_recovery_25deg_continue | v6.2.2 : 6.2.1 추가학습 | 98.8% | 0.020 | 4.0% | ✅ |
| 076 | 05/25 | spotmicro_v6_2_3_prefall_tilt_recovery_30deg | v6.2.3 : recovery 각도 확장 | 92.3% | 0.023 | 4.0% | ✅ |
| 077 | 05/25 | spotmicro_v6_2_4_prefall_tilt_recovery_30deg_continue | v6.2.4 : recovery 각도 확장 추가 학습 | 94.8% | 0.023 | 4.4% | ❌ |
| 078 | 05/25 | spotmicro_v6_2_5_prefall_tilt_recovery_30deg_authority | v6.2.5 : 25~30 recovery 강화 | 94.1% | 0.022 | 4.2% | ❌ |
| 079 | 05/25 | spotmicro_v6_2_6_prefall_tilt_recovery_30deg_rollback | v6.2.6 : 설정 롤백 후 추가학습 | 90.8% | 0.021 | 4.4% | ❌ |
| 080 | 05/25 | spotmicro_v6_3_prefall_transition_tilt_sampler | v6.3 : transition tilt sampler 추가 | 76.3% | 0.024 | 5.1% | ❌ |
| 081 | 05/25 | spotmicro_v6_3_1_prefall_transition_tilt_sampler_soft | v6.3.1 : sampler 강도 약화 | 88.9% | 0.021 | 4.4% | ❌ |
| 082 | 05/25 | spotmicro_v6_3_2_prefall_transition_tilt_sampler_focused | v6.3.2 : sampler 각도 범위 집중 | 79.3% | 0.022 | 4.9% | ❌ |
| 083 | 05/25 | spotmicro_v6_3_3_prefall_transition_tilt_sampler_stable | v6.3.3 : roll ang vel 감소 후 추가 학습 | 86.1% | 0.021 | 4.6% | ❌ |
| 084 | 05/25 | spotmicro_v6_4_prefall_brace_mode | v6.4 : brace mode | 82.1% | 0.021 | 4.4% | ❌ |
| 085 | 05/26 | spotmicro_v7_0_slope_terrain_curriculum | v7.0: 지형 학습 시작 | 61.6% | 0.028 | 5.4% | ❌ |
| 086 | 05/26 | spotmicro_v7_0_slope_terrain_curriculum | v7.0: 지형 학습 시작 | 58.6% | 0.028 | 14.2% | ❌ |
| 087 | 05/26 | spotmicro_v7_0_1_slope_terrain_continue | v7.0.1: 지형 학습 추가 학습 | 56.8% | 0.029 | 15.2% | ❌ |
| 088 | 05/26 | spotmicro_v7_1_slope_terrain_gentle_restart | v7.1: 지형 학습 설정 변경 후 학습 | 65.0% | 0.029 | 13.1% | ❌ |
| 089 | 05/26 | spotmicro_v7_1_slope_terrain_gentle_restart | v7.1: 지형 학습 설정 변경 후 학습 - walk-eval | 76.2% | 0.022 | 13.2% | ❌ |
| 090 | 05/26 | spotmicro_v7_1_1_slope_terrain_walk_refine | v7.1.1: 과도한 tilt transition 끄고 학습 | 73.7% | 0.020 | 11.9% | ❌ |
| 091 | 05/26 | spotmicro_v7_1_2_slope_terrain_walk_margin | v7.1.2: termination 보완 되어야함 | 75.7% | 0.022 | 13.3% | ❌ |
| 092 | 05/26 | spotmicro_v7_1_3_slope_terrain_survival_margin | v7.1.3: timeout 비율 보완 | 75.1% | 0.025 | 13.9% | ❌ |
| 093 | 05/30 | spotmicro_v8.0_classic_ik_new | v8.0: 새 IK로 학습 | 93.6% | 0.030 | 1.8% | ✅ |
| 094 | 05/30 | spotmicro_v8_0_1_newIk_feet_air | v8.0.1: 새 IK로 학습, 발 들 수 있게 | 90.9% | 0.037 | 2.7% | ✅ |
| 095 | 05/31 | spotmicro_v8_0_2_new_IK_train | v8.0.2: 새 IK로 학습, 발 들 수 있게 | 94.8% | 0.047 | 7.6% | ✅ |
| 096 | 05/31 | spotmicro_v8_1_new_IK_lin_only | v8.1: 우선 직진부터 학습 | 77.5% | 0.139 | 19.2% | ❌ |
| 097 | 05/31 | spotmicro_v8_2_phase_period_change | v8.2: phase 1초 실험 | 80.9% | 0.112 | 19.1% | ✅ |
