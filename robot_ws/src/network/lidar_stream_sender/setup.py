from setuptools import setup
import os
from glob import glob

package_name = 'lidar_stream_sender'

setup(
    name=package_name,
    version='0.1.0',
    packages=[package_name],
    data_files=[
        ('share/ament_index/resource_index/packages',
         [f'resource/{package_name}']),
        (f'share/{package_name}', ['package.xml']),
        (f'share/{package_name}/launch',
         glob('launch/*.launch.py')),
        (f'share/{package_name}/config',
         glob('config/*.yaml')),
    ],
    install_requires=['setuptools'],
    zip_safe=True,
    maintainer='todo',
    maintainer_email='todo@todo.com',
    description='/scan_3D → UDP → RPi BridgeDaemon 송신 노드',
    license='TODO',
    entry_points={
        'console_scripts': [
            'lidar_stream_sender_node = '
            'lidar_stream_sender.lidar_stream_sender_node:main',
        ],
    },
)
