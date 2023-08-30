
/**
 * This file is part of dirage2.
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#ifndef DIRMODEL_H
#define DIRMODEL_H

#include "dirtree.h"
#include "agechart.h"

#include <QAbstractItemModel>
#include <QFutureWatcher>
#include <QHash>

class DirModel final : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Columns { C_NAME, C_TYPE, C_SIZE, C_MEDIAN_AGE, C_AGE, C_SENTINEL };
    enum Types { T_SUBDIR, T_FILE, T_SENTINEL };
    enum UserRoles { R_TOTALSIZE = Qt::UserRole+1, R_MINAGE, R_MAXAGE, R_SIZE, R_SORT, R_SENTINEL };

    explicit DirModel(QObject *parent = nullptr);
    ~DirModel();
    void reset(DirTree *newTree);
    void calculated(QModelIndex index, AgeChart chart);
    bool isChartCached(QModelIndex index);

    enum class IndexTarget { INVALID, ITSELF, FILES };
    QPair<DirTree*, IndexTarget> indexToDirTree(QModelIndex index) const;
    QModelIndex dirTreeToIndex(DirTree* tree) const;

    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    QModelIndex index(int row, int column,
                      const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &index) const override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

private:
    DirTree*                        m_tree;
    qint64                          m_chartsMin;
    qint64                          m_chartsMax;
    QDateTime                       m_resetTime;
    QHash<QModelIndex, AgeChart>    m_charts;
};


#endif // DIRMODEL_H
