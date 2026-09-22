import os
from glob import glob
from setuptools import find_packages, setup

package_name = "rl_locomotion"

setup(
    name=package_name,
    version="0.0.0",
    packages=find_packages(exclude=["test"]),
    data_files=[
        (
            "share/ament_index/resource_index/packages",
            ["resource/" + package_name],
        ),
        ("share/" + package_name, ["package.xml"]),
        (
            os.path.join("share", package_name, "launch"),
            glob("launch/*.launch.py"),
        ),
        (
            os.path.join("share", package_name, "config"),
            glob("config/*.yaml"),
        ),
        (
            os.path.join("share", package_name, "models"),
            glob("models/*"),
        ),
    ],
    install_requires=["setuptools"],
    scripts=[
        "tools/analyze_kp_kd_response.py",
    ],
    zip_safe=True,
    maintainer="jetson",
    maintainer_email="jetson@todo.todo",
    description="SpotMicro RL locomotion runtime package",
    license="TODO",
    extras_require={
        "test": [
            "pytest",
        ],
    },
    entry_points={
        "console_scripts": [
            "rl_locomotion_node = rl_locomotion.rl_locomotion_node:main",
            "kp_kd_response_recorder = rl_locomotion.kp_kd_response_recorder:main",
        ],
    },
)
