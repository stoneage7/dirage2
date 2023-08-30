
/**
 * This file is part of dirage2.
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#ifndef AGECHART_H
#define AGECHART_H

#include <QFuture>
#include <QVariant>

struct AgeChart
{
    qint64      min;
    qint64      lowerWhisker;
    qint64      lowerQuartile;
    qint64      median;
    qint64      upperQuartile;
    qint64      upperWhisker;
    qint64      max;

    AgeChart();
    bool valid() const;
    bool singleton() const;
};

Q_DECLARE_METATYPE(AgeChart);

#endif // AGECHART_H
