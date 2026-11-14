/* Copyright (C) 2023-2026 CascadiaVoxel LLC

    nanoPRC is free software: you can redistribute it and/or modify it under
    the terms of the GNU Affero General Public License as published by the
    Free Software Foundation, either version 3 of the License, or (at your
    option) any later version.

    nanoPRC is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public
    License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with nanoPRC. If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef _UTIL_H_
#define _UTIL_H_

#include <mutil/mutil.h>

using namespace mutil;

#define colorRGB(r, g, b) (Vector3((r) / 255.0f, (g) / 255.0f, (b) / 255.0f))

constexpr Vector3 rgbToHsv(const Vector3 &rgb)
{
    constexpr float kEpsilon = 0.0001f;
    constexpr Vector4 kOffsets(0.0f, -1.0f / 3.0f, 2.0f / 3.0f, -1.0f);

    Vector4 a = rgb.b > rgb.g
                    ? Vector4(rgb.b, rgb.g, kOffsets.w, kOffsets.z)
                    : Vector4(rgb.g, rgb.b, kOffsets.x, kOffsets.y);
    Vector4 b = rgb.r > a.x
                    ? Vector4(rgb.r, a.y, a.z, a.x)
                    : Vector4(a.x, a.y, a.y, rgb.r);

    float m = mutil::min(b.y, b.w);

    float C = b.x - m;
    float V = b.x;
    float H = mutil::abs(b.z + (b.w - b.y) / (6.0f * C + kEpsilon));

    return Vector3(H, C / (b.x + kEpsilon), V);
}

constexpr Vector3 hueToRgb(float H)
{
    float r = mutil::abs(H * 6.0f - 3.0f) - 1.0f;
    float g = 2.0f - mutil::abs(H * 6.0f - 2.0f);
    float b = 2.0f - mutil::abs(H * 6.0f - 4.0f);
    return mutil::clamp(Vector3(r, g, b), 0.0f, 1.0f);
}

constexpr Vector3 hsvToRgb(const Vector3 &hsv)
{
    Vector3 rgb = hueToRgb(hsv.x);
    float C = hsv.z * hsv.y;
    return rgb * C + Vector3(hsv.z - C);
}

#endif // _UTIL_H_
