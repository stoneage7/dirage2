
/**
 * This file is part of dirage2.
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#include "agechart.h"
#include <limits>

constexpr qint64 LOW = std::numeric_limits<decltype(AgeChart::min)>::lowest();

AgeChart::AgeChart()
{
    min = LOW; lowerWhisker = LOW; lowerQuartile = LOW; median = LOW;
    upperQuartile = LOW; upperWhisker = LOW; max = LOW;
}

bool AgeChart::valid() const
{
    return min > LOW && lowerWhisker >= min && lowerQuartile >= lowerWhisker &&
            median >= lowerWhisker && upperQuartile >= median && upperWhisker >= upperQuartile &&
            max >= upperQuartile;
}

bool AgeChart::singleton() const
{
    return min > LOW && lowerWhisker == min && lowerQuartile == lowerWhisker &&
            median == lowerWhisker && upperQuartile == median && upperWhisker == upperQuartile &&
            max == upperQuartile;
}
