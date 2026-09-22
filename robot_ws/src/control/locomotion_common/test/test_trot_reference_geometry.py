import math

from locomotion_common.trot_reference import SharedTrotReference


def fk_contact(leg, theta_shoulder, theta_leg, theta_foot):
    shoulder_axis = [1.0, -1.0, 1.0, -1.0][leg]
    shoulder_y = [0.052, -0.052, 0.052, -0.052][leg]
    upper_x = 0.010
    upper_z = 0.120
    lower = 0.115
    toe_radius = 0.015

    knee_x = upper_x * math.cos(theta_leg) + upper_z * math.sin(theta_leg)
    knee_z = upper_x * math.sin(theta_leg) - upper_z * math.cos(theta_leg)
    toe_x = knee_x + lower * math.sin(theta_leg + theta_foot)
    toe_z = knee_z - lower * math.cos(theta_leg + theta_foot)

    shoulder_physical = shoulder_axis * theta_shoulder
    cos_s = math.cos(shoulder_physical)
    sin_s = math.sin(shoulder_physical)
    y = shoulder_y * cos_s - toe_z * sin_s
    z = shoulder_y * sin_s + toe_z * cos_s
    return toe_x, y, z - toe_radius


def test_default_reference_matches_spotmicro_urdf_contact_geometry():
    ref = SharedTrotReference(
        body_height=0.210,
        default_foot_x=(0.0, 0.0, 0.0, 0.0),
        default_foot_y=(0.052, -0.052, 0.052, -0.052),
        shoulder_offset_y=(0.052, -0.052, 0.052, -0.052),
        upper_link_x=0.010,
        upper_link_z=0.120,
        lower_link=0.115,
        toe_radius=0.015,
    )

    target = ref.get_reference(phase=0.0, cmd_vx=0.0, cmd_vy=0.0, cmd_wz=0.0)

    for leg in range(4):
        base = leg * 3
        x, y, z = fk_contact(
            leg,
            target[base + 0],
            target[base + 1],
            target[base + 2],
        )
        expected_y = 0.052 if leg in (0, 2) else -0.052

        assert math.isclose(target[base + 0], 0.0, abs_tol=1.0e-9)
        assert math.isclose(x, 0.0, abs_tol=1.0e-9)
        assert math.isclose(y, expected_y, abs_tol=1.0e-9)
        assert math.isclose(z, -0.210, abs_tol=1.0e-9)


def test_default_reference_stays_inside_urdf_joint_limits():
    ref = SharedTrotReference()

    target = ref.get_reference(phase=0.0, cmd_vx=0.0, cmd_vy=0.0, cmd_wz=0.0)

    for i in range(0, 12, 3):
        assert -0.548 <= target[i] <= 0.548
        assert -2.666 <= target[i + 1] <= 1.548
        assert -0.100 <= target[i + 2] <= 2.590
