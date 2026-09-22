#ifndef LIDAR_PERCEPTION__SCAN_UTILS_HPP_
#define LIDAR_PERCEPTION__SCAN_UTILS_HPP_

#include <cmath>
#include <vector>

namespace lidar_perception
{
    // LiDAR sector 각도 범위를 표현하는 구조체
    struct SectorRange
    {
        float min_angle_rad;
        float max_angle_rad;
    };

    // Cluster 정보를 담는 구조체
    struct ScanCluster
    {
        int start_index;
        int end_index;
        int point_count;

        float start_angle_rad;
        float end_angle_rad;
        float center_angle_rad;

        float min_distance;
    };

    // degree 단위를 radian으로 변환
    inline float degToRad(float degree)
    {
        return degree * static_cast<float>(M_PI) / 180.0f;
    }

    // Debug 편의를 위한 radian → degree 변환 함수
    inline float radToDeg(float radian)
    {
        return radian * 180.0f / static_cast<float>(M_PI);
    }

    // 특정 angle이 특정 sector 범위 안에 있는지 확인
    inline bool isAngleInSector(float angle, const SectorRange& sector) 
    {
        return angle >= sector.min_angle_rad && angle <= sector.max_angle_rad;
    }

    // LaserScan index를 실제 angle(rad)로 변환
    inline float indexToAngle(int index, float angle_min, float angle_increment)
    {
        return angle_min + static_cast<float>(index) * angle_increment;
    }

    // range 값이 유효한지 확인
    inline bool isValidRange(float range, float range_min, float range_max, float effective_range_max) 
    {
        return std::isfinite(range) && range >= range_min && range <= range_max && range <= effective_range_max;
    }
}

#endif