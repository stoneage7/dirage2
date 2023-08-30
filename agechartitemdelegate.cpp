
/**
 * This file is part of dirage2.
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#include "agechartitemdelegate.h"
#include "agechart.h"
#include "dirmodel.h"
#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <cmath>

template <double(*Func)(double)>
static QVariant ratioOf(const QVariant &numerator, const QVariant &denominator)
{
    if (!numerator.canConvert<qint64>() || !denominator.canConvert<qint64>()) {
        return QVariant();
    }
    qint64 _numerator = numerator.value<qint64>();
    qint64 _denominator = denominator.value<qint64>();
    if (_numerator < 0 || _denominator < 0) {
        return QVariant();
    }
    double numerator_sqrt = Func(static_cast<double>(_numerator));
    double denominator_sqrt = Func(static_cast<double>(_denominator));
    if (denominator_sqrt == 0.0) {
        if (numerator_sqrt == 0.0) {
            return QVariant(1.0);
        } else {
            return QVariant();
        }
    }
    return QVariant(numerator_sqrt / denominator_sqrt);
}

static double identity(double x)
{ return x; }

AgeChartItemDelegate::AgeChartItemDelegate(QObject* parent):
    QStyledItemDelegate(parent)

{
    m_rowHeight = QApplication::style()->pixelMetric(QStyle::PM_LargeIconSize);
    calculateColors();
}

void AgeChartItemDelegate::paint(QPainter *painter,
                                 const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const
{
    if (index.column() != DirModel::C_AGE) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    QVariant v = index.data(Qt::DisplayRole);
    auto paintBusy = [this, painter, option, index]() {
        QIcon icon = QIcon::fromTheme(QStringLiteral("clock"));
        icon.paint(painter, option.rect);
    };

    auto size = index.data(DirModel::R_SIZE);
    if (size == 0) {
        return;
    }

    AgeChart chartCoords = v.value<AgeChart>();
    int boxHeight = 0;

    if (!chartCoords.valid()) {
        paintBusy();
        return;
    }
    QVariant minAge = index.data(DirModel::R_MINAGE);
    if (minAge.isNull() || !minAge.canConvert<qint64>()) {
        paintBusy();
        return;
    }
    QVariant maxAge = index.data(DirModel::R_MAXAGE);
    if (maxAge.isNull() || !maxAge.canConvert<qint64>()) {
        paintBusy();
        return;
    }

    if (minAge.value<qint64>() == maxAge.value<qint64>()) {
        // Bail if we can't determine width of the chart.
        return;
    }

    QVariant relHeight;
    QVariant totalSize = index.data(DirModel::R_TOTALSIZE);
    switch (m_scaling) {
    case Scaling::S_SQRT:
        relHeight = ratioOf<&std::sqrt>(size, totalSize);
        break;
    case Scaling::S_LN:
        relHeight = ratioOf<&std::log10>(size, totalSize);
        break;
    case Scaling::S_LINEAR:
        relHeight = ratioOf<&identity>(size, totalSize);
        break;
    default:
        relHeight = 1;
        break;
    }

    if (relHeight.isNull() || !relHeight.canConvert<double>()) {
        paintBusy();
        return;
    }

    auto xPos = [minAge, maxAge, rect = option.rect] (qint64 value) {
        qint64 _min = minAge.value<qint64>();
        qint64 _max = maxAge.value<qint64>();
        if (_min == _max)
            return 0LL;  // This shouldn't happen but shup up Clang.
        else
            return rect.x() + (rect.width() - 1) * (value - _min) / (_max - _min);
    };

    boxHeight = static_cast<int>(relHeight.value<double>() * (option.rect.height() - 2));
    boxHeight = qMax(3, boxHeight);
    int boxTop = option.rect.y() + (option.rect.height() - boxHeight) / 2;
    int boxBottom = boxTop + boxHeight;
    int boxMid = (boxTop + boxBottom) / 2;
    QColor boxColor = QColor(200, 200, 200);

    if (chartCoords.singleton()) {
        painter->drawLine(xPos(chartCoords.median),
                          boxTop,
                          xPos(chartCoords.median),
                          boxBottom);
        return;
    }

    // Draw the box plot elements.
    painter->save();

    painter->setPen(m_penColor);
    painter->setBrush(m_fillColor);

    painter->drawRect(xPos(chartCoords.lowerQuartile),
                      boxTop,
                      xPos(chartCoords.upperQuartile) - xPos(chartCoords.lowerQuartile),
                      boxBottom - boxTop);

    // Draw the whiskers
    painter->drawLine(xPos(chartCoords.lowerQuartile),
                      boxMid,
                      xPos(chartCoords.lowerWhisker),
                      boxMid);
    painter->drawLine(xPos(chartCoords.lowerWhisker),
                      boxTop,
                      xPos(chartCoords.lowerWhisker),
                      boxBottom);
    painter->drawLine(xPos(chartCoords.upperQuartile),
                      boxMid,
                      xPos(chartCoords.upperWhisker),
                      boxMid);
    painter->drawLine(xPos(chartCoords.upperWhisker),
                      boxTop,
                      xPos(chartCoords.upperWhisker),
                      boxBottom);

    painter->setPen(m_medianColor);

    // Draw the median line
    painter->drawLine(xPos(chartCoords.median),
                      boxTop + 1,
                      xPos(chartCoords.median),
                      boxBottom - 1);

    painter->restore();
}

QSize AgeChartItemDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QSize hint = QStyledItemDelegate::sizeHint(option, index);
    hint.setHeight(m_rowHeight);
    return hint;
}

void AgeChartItemDelegate::calculateColors()
{
    QPalette palette = QApplication::palette();
    QColor bgColor = palette.color(QPalette::Active, QPalette::Window);

    m_penColor = palette.color(QPalette::Active, QPalette::WindowText);

    QColor penHsl = m_penColor.toHsl();
    QColor bgHsl = bgColor.toHsl();

    int avgLightness = (penHsl.lightness() + bgHsl.lightness()) / 2;
    int quarterLightness = ((avgLightness + bgHsl.lightness()) / 2 + avgLightness) / 2;

    m_fillColor = QColor::fromHsl(penHsl.hslHue(), penHsl.hslSaturation(), avgLightness).toRgb();

    if (bgHsl.saturation() < 100) {
        m_medianColor = QColor::fromHsl(0, 200, quarterLightness).toRgb();
    }
    else {
        m_medianColor = QColor::fromHsl((penHsl.hslHue() + 180) % 360,
                                        penHsl.hslSaturation(), quarterLightness).toRgb();
    }
}

void AgeChartItemDelegate::setScaling(Scaling s)
{
    m_scaling = s;
}
