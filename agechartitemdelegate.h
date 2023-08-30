
/**
 * This file is part of dirage2.
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#ifndef AGECHARTITEMDELEGATE_H
#define AGECHARTITEMDELEGATE_H

#include <QStyledItemDelegate>
#include <QColor>

class AgeChartItemDelegate final : public QStyledItemDelegate
{
    Q_OBJECT
public:
    AgeChartItemDelegate(QObject* parent = nullptr);

    // QAbstractItemDelegate interface
public:
    virtual void paint(QPainter *painter,
                       const QStyleOptionViewItem &option,
                       const QModelIndex &index) const override;

    virtual QSize sizeHint(const QStyleOptionViewItem &option,
                           const QModelIndex &index) const override;

    enum class Scaling { S_LINEAR, S_SQRT, S_LN };

public slots:
    void calculateColors();
    void setScaling(Scaling s);

private:
    int m_rowHeight;
    QColor m_penColor;
    QColor m_fillColor;
    QColor m_medianColor;
    Scaling m_scaling;
};

#endif // AGECHARTITEMDELEGATE_H
