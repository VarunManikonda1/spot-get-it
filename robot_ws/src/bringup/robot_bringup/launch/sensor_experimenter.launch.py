from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription, OpaqueFunction
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory

import os
from pathlib import Path


def find_next_session_index(output_root: str) -> str:
    """
    /home/jetson/data 아래의 숫자 폴더를 확인해서 다음 실험 번호를 반환한다.

    예:
      기존:
        /home/jetson/data/1
        /home/jetson/data/2

      반환:
        "3"
    """

    root_path = Path(output_root).expanduser()
    root_path.mkdir(parents=True, exist_ok=True)

    existing_indices = []

    for child in root_path.iterdir():
        if child.is_dir() and child.name.isdigit():
            existing_indices.append(int(child.name))

    next_index = max(existing_indices) + 1 if existing_indices else 1

    # 상위 세션 폴더만 먼저 만든다.
    # 하위 video/, lidar/는 각 collector가 만들게 한다.
    session_dir = root_path / str(next_index)
    session_dir.mkdir(parents=True, exist_ok=False)

    return str(next_index)


def launch_setup(context, *args, **kwargs):
    robot_id = LaunchConfiguration("robot_id").perform(context)
    output_root = LaunchConfiguration("output_root").perform(context)
    session_index_arg = LaunchConfiguration("session_index").perform(context)

    # session_index:=0 이면 자동으로 다음 번호 선택
    # session_index:=1 처럼 넣으면 해당 번호 사용
    if session_index_arg in ["", "0", "auto"]:
        session_index = find_next_session_index(output_root)
    else:
        session_index = session_index_arg

        session_dir = Path(output_root).expanduser() / session_index
        session_dir.mkdir(parents=True, exist_ok=True)

    video_collector = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            os.path.join(
                get_package_share_directory("video_collector"),
                "launch",
                "video_collector.launch.py",
            )
        ),
        launch_arguments={
            "robot_id": robot_id,
            "output_root": output_root,
            "session_index": session_index,
            "sensor_subdir": "video",
        }.items(),
    )

    lidar_collector = Node(
        package="lidar_collector",
        executable="pointcloud2_jsonl_recorder",
        name="pointcloud2_jsonl_recorder",
        parameters=[{
            "robot_id": robot_id,
            "source_topic": "/perception/lidar/points_filtered",
            "output_root": output_root,
            "sensor_subdir": "lidar",
            "session_index": int(session_index),
        }],
        output="screen",
    )

    return [
        video_collector,
        lidar_collector,
    ]


def generate_launch_description():
    robot_id_arg = DeclareLaunchArgument(
        "robot_id",
        default_value="spot_02",
        description="로봇 식별자 예: spot_01, spot_02, spot_03",
    )

    output_root_arg = DeclareLaunchArgument(
        "output_root",
        default_value="/home/jetson/data",
        description="센서 데이터 저장 root 경로",
    )

    session_index_arg = DeclareLaunchArgument(
        "session_index",
        default_value="0",
        description="실험 번호. 0이면 /home/jetson/data 아래 다음 번호를 자동 선택",
    )

    return LaunchDescription([
        robot_id_arg,
        output_root_arg,
        session_index_arg,
        OpaqueFunction(function=launch_setup),
    ])