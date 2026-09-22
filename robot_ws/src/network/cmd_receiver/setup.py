from setuptools import setup
from glob import glob

package_name = 'cmd_receiver'

setup(
    name=package_name,
    version='0.0.1',
    packages=[package_name],
    data_files=[
        ('share/ament_index/resource_index/packages',
            ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
        ('share/' + package_name + '/launch', glob('launch/*.launch.py')),
    ],
    install_requires=['setuptools'],
    zip_safe=True,
    maintainer='jetson',
    maintainer_email='jetson@example.com',
    description='UDP 명령 수신 후 ROS2 토픽 publish 노드',
    license='MIT',
    entry_points={
        'console_scripts': [
            'cmd_receiver_node = cmd_receiver.cmd_receiver_node:main',
            'ble_button_mode_receiver = cmd_receiver.ble_button_mode_receiver:main',
        ],
    },
)
