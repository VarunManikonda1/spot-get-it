# network_bringup

Network sender bringup package.

## Run

```bash
ros2 launch network_bringup network_bringup.launch.py
```

## Launch Arguments

| Name | Default | Description |
| --- | --- | --- |
| `rpi5_ip` | `192.168.0.13` | BridgeDaemon target IP passed to `pose_path_event_sender` |
| `enable_pose_path_event` | `true` | Launch `pose_path_event_sender` |
| `enable_lidar` | `true` | Launch `lidar_stream_sender` |
