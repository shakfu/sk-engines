// Copyright 2024 Infrasonic Audio LLC
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
#include "color.h"
#include <algorithm>

// Size-optimize: color conversion helpers feed only the 62 Hz LED render, never the audio path.
#pragma GCC optimize("Os")

using namespace infrasonic;

Color Color::FromHSV(uint8_t h, uint8_t s, uint8_t v)
{
    uint8_t p, q, t;
    uint8_t r, g, b;
    uint8_t region, remainder;

    if(s == 0)
    {
        return Color(0);
    }

    region    = h / 43;
    remainder = (h - (region * 43)) * 6;

    p = (v * (255 - s)) >> 8;
    q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

    switch(region)
    {
        case 0:
            r = v;
            g = t;
            b = p;
            break;
        case 1:
            r = q;
            g = v;
            b = p;
            break;
        case 2:
            r = p;
            g = v;
            b = t;
            break;
        case 3:
            r = p;
            g = q;
            b = v;
            break;
        case 4:
            r = t;
            g = p;
            b = v;
            break;
        default:
            r = v;
            g = p;
            b = q;
            break;
    }

    return Color(r, g, b);
}

Color Color::operator+(const float offset) const
{
    return Color(red_ + offset, green_ + offset, blue_ + offset);
}

Color Color::operator-(const float offset) const
{
    return Color(red_ - offset, green_ - offset, blue_ - offset);
}

Color Color::operator+(const Color& other) const
{
    return Color(red_ + other.red_, green_ + other.green_, blue_ + other.blue_);
}

Color Color::operator-(const Color& other) const
{
    return Color(red_ - other.red_, green_ - other.green_, blue_ - other.blue_);
}

Color Color::operator*(const float scale) const
{
    return Color(red_ * scale, green_ * scale, blue_ * scale);
}

Color Color::operator*(const Color& other) const
{
    return Color(red_ * other.red_, green_ * other.green_, blue_ * other.blue_);
}

Color& Color::operator+=(const float offset)
{
    red_   = unitclamp(red_ + offset);
    green_ = unitclamp(green_ + offset);
    blue_  = unitclamp(blue_ + offset);
    return *this;
}

Color& Color::operator+=(const Color& other)
{
    red_   = unitclamp(red_ + other.red_);
    green_ = unitclamp(green_ + other.green_);
    blue_  = unitclamp(blue_ + other.blue_);
    return *this;
}

Color& Color::operator-=(const float offset)
{
    red_   = unitclamp(red_ - offset);
    green_ = unitclamp(green_ - offset);
    blue_  = unitclamp(blue_ - offset);
    return *this;
}

Color& Color::operator-=(const Color& other)
{
    red_   = unitclamp(red_ - other.red_);
    green_ = unitclamp(green_ - other.green_);
    blue_  = unitclamp(blue_ - other.blue_);
    return *this;
}

Color& Color::operator*=(const float scale)
{
    red_   = unitclamp(red_ * scale);
    green_ = unitclamp(green_ * scale);
    blue_  = unitclamp(blue_ * scale);
    return *this;
}

Color& Color::operator*=(const Color& other)
{
    red_   = unitclamp(red_ * other.red_);
    green_ = unitclamp(green_ * other.green_);
    blue_  = unitclamp(blue_ * other.blue_);
    return *this;
}
