#include "leg_ik.h"
#include <math.h>

#define M_PI_F  3.14159265358979f

void ik_2link_to_raw(float L1_mm, float L2_mm,
                     float foot_x_mm, float foot_z_mm,
                     int *thigh_offset_raw, int *knee_offset_raw)
{
    float r2 = foot_x_mm * foot_x_mm + foot_z_mm * foot_z_mm;
    float cos_k = (r2 - L1_mm*L1_mm - L2_mm*L2_mm) / (2.0f * L1_mm * L2_mm);
    if (cos_k > 1.0f) cos_k = 1.0f;
    if (cos_k < -1.0f) cos_k = -1.0f;
    float sin_k = sqrtf(1.0f - cos_k * cos_k);
    float theta_k = atan2f(sin_k, cos_k);

    float A = L1_mm + L2_mm * cos_k;
    float B = L2_mm * sin_k;
    float det = A*A + B*B;
    float foot_z_neg = -foot_z_mm;
    float sin_t = (A * foot_x_mm - B * foot_z_neg) / det;
    float cos_t = (B * foot_x_mm + A * foot_z_neg) / det;
    float theta_t = atan2f(sin_t, cos_t);

    float t_deg = theta_t * 180.0f / M_PI_F;
    float k_deg = theta_k * 180.0f / M_PI_F;

    *thigh_offset_raw = (int)roundf(t_deg * 4096.0f / 360.0f);
    *knee_offset_raw  = (int)roundf(k_deg * 4096.0f / 360.0f);
}
