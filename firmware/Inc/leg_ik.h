#ifndef LEG_IK_H
#define LEG_IK_H

/*
 * 2-link planar leg IK.
 * Hip frame: x = forward+/back-, z = up+/down- (foot below hip → z 음수).
 * 입력: foot 위치 (foot_x_mm, foot_z_mm), 링크 길이 (L1, L2).
 * 출력: thigh, knee 각각 ZERO_POS 기준 raw offset (4096 = 360°).
 *
 * 신글럴리티/언리치블 영역에서도 cos_k clamp 후 가까운 해 반환.
 */
void ik_2link_to_raw(float L1_mm, float L2_mm,
                     float foot_x_mm, float foot_z_mm,
                     int *thigh_offset_raw, int *knee_offset_raw);

#endif /* LEG_IK_H */
