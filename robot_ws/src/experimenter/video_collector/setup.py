from setuptools import find_packages, setup
import os
from glob import glob

package_name = 'video_collector'

setup(
    name=package_name,
    version='0.0.1',
    packages=find_packages(exclude=['test']),
    data_files=[
        ('share/ament_index/resource_index/packages', ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
        (os.path.join('share', package_name, 'launch'), glob('launch/*.py')),
        (os.path.join('share', package_name, 'config'), glob('config/*.yaml')),
    ],
    install_requires=['setuptools'],
    zip_safe=True,
    maintainer='spot_get_it',
    description='OAK-D Lite MJPEG 영상 수집 노드',
    entry_points={
        'console_scripts': [
            'video_collector_node = video_collector.video_collector_node:main',
        ],
    },
)
