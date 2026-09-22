from robot_interfaces.msg import JointTarget


NUM_JOINTS = 12
NUM_LEGS = 4

# FL, FR, RL, RR. Matches firmware/docs/architecture.md and gait.c.
PHASE_OFFSET = [0.5, 0.0, 0.0, 0.5]

MODE_CLASSIC_CONTROL = getattr(
    JointTarget,
    "MODE_CLASSIC",
    getattr(JointTarget, "MODE_CLASSIC_CONTROL", JointTarget.MODE_RL),
)
