
/**
 * This file is part of dirage2.
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#include "dirmodel.h"
#include <QDateTime>
#include <QPair>
#include <limits>

static QString displayFileSize(qint64 sizeInBytes)
{
    static const char *units[] = { "B", "KiB", "MiB", "GiB", "TiB" };
    constexpr int cntUnits = sizeof(units) / sizeof(units[0]);
    int divisor = 0;
    double size = static_cast<double>(sizeInBytes);
    while (size >= 1024.0 && divisor < (cntUnits  - 1)) {
        size /= 1024.0;
        ++divisor;
    }
    return QStringLiteral("%1 %2").arg(size, 0, 'f', 1).arg(units[divisor]);
}

static QString fuzzyDuration(qint64 timestamp, const QDateTime &current)
{
    QDateTime dt1 = QDateTime::fromSecsSinceEpoch(timestamp);
    qint64 seconds = dt1.secsTo(current);
    qint64 minutes = seconds / 60;
    qint64 hours = minutes / 60;
    qint64 days = hours / 24;

    QDate d1 = dt1.date();
    QDate d2 = current.date();
    int years = d2.year() - d1.year();
    int months = d2.month() - d1.month();
    if (months < 0) {
        years--;
        months += 12;
    }
    if (years > 0)
        return QStringLiteral("%1yr %2mo").arg(years).arg(months);
    else if (months > 0)
        return QStringLiteral("%1mo").arg(months);
    else if (days > 6)
        return QStringLiteral("%1wk").arg(days / 7);
    else if (days > 0)
        return QStringLiteral("%1d").arg(days);
    else if (hours > 0)
        return QStringLiteral("%1h %2m").arg(hours % 24).arg(minutes % 60);
    else if (minutes > 0)
        return QStringLiteral("%1m").arg(minutes);
    else
        return QStringLiteral("%1sec").arg(seconds);
}

constexpr qint64 LOW = std::numeric_limits<qint64>::lowest();
constexpr qint64 HIGH = std::numeric_limits<qint64>::max();

DirModel::DirModel(QObject *parent)
    : QAbstractItemModel(parent)
{
    m_tree = nullptr;
    m_chartsMin = HIGH;
    m_chartsMax = LOW;
    m_resetTime = QDateTime::currentDateTime();
}

DirModel::~DirModel()
{
    delete m_tree;
}

static void dumptree(DirTree *d, int level=0)
{
    auto n = d->numChildren();
    qDebug() << QStringLiteral(" ").repeated(level) + d->name() << "parent" << d->parent() << d->parentPos() << "nch" << d->numChildren() << "size" << d->subtreeSize() << "ptr" << (void*)&d;
    for (auto i = 0; i < n; i++) {
        dumptree(d->child(i), level+1);
    }
}

void DirModel::reset(DirTree *newTree)
{
    beginResetModel();
    m_chartsMin = HIGH;
    m_chartsMax = LOW;
    if (m_tree) delete m_tree;
    m_tree = newTree;
    //dumptree(m_tree);
    m_resetTime = QDateTime::currentDateTime();
    m_charts.clear();
    m_charts.squeeze();
    endResetModel();
}

void DirModel::calculated(QModelIndex index, AgeChart chart)
{
    if (chart.valid()) {
        m_charts[index.siblingAtColumn(0)] = chart;
        if (m_chartsMin > chart.lowerWhisker) {
            m_chartsMin = chart.lowerWhisker;
        }
        if (m_chartsMax < chart.upperWhisker) {
            m_chartsMax = chart.upperWhisker;
        }
        emit dataChanged(index.siblingAtColumn(0), index.siblingAtColumn(C_SENTINEL - 1));
    }
}

bool DirModel::isChartCached(QModelIndex index)
{
    return m_charts.contains(index);
}

//! Returns pointer to the struct in the tree. The logic is such that the internal pointer
//! points to the parent of the DirTree at this index. See index().
QPair<DirTree *, DirModel::IndexTarget> DirModel::indexToDirTree(QModelIndex index) const
{
    DirTree *inPtr = static_cast<DirTree*>(index.internalPointer());
    if (!index.isValid()) {
        return qMakePair(nullptr, IndexTarget::INVALID);
    }
    else if (inPtr != nullptr && index.row() >= 0) {
        if (index.row() < inPtr->numChildren()) {
            // The first n rows point to subtrees.
            return qMakePair(inPtr->child(index.row()), IndexTarget::ITSELF);
        }
        else if (index.row() == inPtr->numChildren()) {
            // The last row points to files.
            return qMakePair(inPtr, IndexTarget::FILES);
        }
        else {
            return qMakePair(nullptr, IndexTarget::INVALID);
        }
    }
    else if (inPtr == nullptr) {
        // Index of root case. Internal pointer is null.
        return qMakePair(m_tree, IndexTarget::ITSELF);
    }
    else {
        return qMakePair(nullptr, IndexTarget::INVALID);
    }
}

QModelIndex DirModel::dirTreeToIndex(DirTree *tree) const
{
    return createIndex(tree->parentPos(), 0, tree->parent());
}

QVariant DirModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        switch (section) {
        case C_NAME:
            return QVariant("Name");
        case C_TYPE:
            return QVariant("Type");
        case C_SIZE:
            return QVariant("Size");
        case C_MEDIAN_AGE:
            return QVariant("Median Age");
        case C_AGE:
            return QVariant("Age");
        }
    }
    return QVariant();
}

/* Internal pointer will always point to the parent DirTree structure.
 * If the index at first level of the tree, the internal pointer will be m_tree.
 * Consider the following structure:
 * m_tree              (root) parent is invalid => out: ip=nullptr row=0
 *  a                  parent.ip=nullptr => out: ip=m_tree row=0
 *   b                 parent.ip=m_tree parent.row=0 => out: ip=a row=0
 *   c                 parent.ip=m_tree parent.row=0 => out: ip=a row=1
 *    d                parent.ip=a parent.row=1 => out: ip=c row=0
 *    e                parent.ip=a parent.row=1 => out: ip=c row=1
 *    files            parent.ip=a parent.row=1 => out: ip=c row=2
 *   files             parent.ip=m_tree parent.row=0 => out: ip=a row=2
 *  x                  parent.ip=nullptr => out: ip=m_tree row=1
 *  files              parent.ip=nullptr => out: ip=m_tree row=2
 */
QModelIndex DirModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!parent.isValid()) {
        return createIndex(row, column, nullptr);
    }
    else {
        auto p = indexToDirTree(parent);
        switch (p.second) {
        default:
        case IndexTarget::INVALID:
            return QModelIndex();
        case IndexTarget::FILES:
            // Cannot have files as a parent.
            return QModelIndex();
        case IndexTarget::ITSELF:
            return createIndex(row, column, const_cast<DirTree*>(p.first));
        }
    }
}

QModelIndex DirModel::parent(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return QModelIndex();
    }
    else {
        auto p = indexToDirTree(index);
        switch (p.second) {
        default:
        case IndexTarget::INVALID:
            return QModelIndex();
        case IndexTarget::FILES:
            if (p.first != nullptr) {
                // p is pointing to the parent, so we create index with i.p. to its parent.
                return createIndex(p.first->parentPos(), 0, p.first->parent());
            }
            else {
                //qDebug() << " 3. ()";
                return QModelIndex();
            }
        case IndexTarget::ITSELF:
            // p is pointing to a subtree, so we go up one and create index to that.
            auto pp = p.first->parent();
            if (pp != nullptr) {
                return createIndex(pp->parentPos(), 0, pp->parent());
            }
            else {
                // parent is null, which means index is the root node.
                return QModelIndex();
            }
        }
    }
}

int DirModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid()) {
        if (m_tree == nullptr)
            return 0;  // Have nothing.
        else
            return 1;  // One item at the top -- directory chosen by the user.
    }
    else {
        auto p = indexToDirTree(parent);
        switch (p.second) {
        default:
        case IndexTarget::INVALID:
        case IndexTarget::FILES:
            return 0;
        case IndexTarget::ITSELF:
            return p.first->numChildren() + ((p.first->numFiles() > 0) ? 1 : 0);
        }
    }
}

int DirModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return C_SENTINEL;
    else
        return C_SENTINEL;
}

QVariant DirModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }
    else if (role > Qt::UserRole && role < R_SENTINEL) {
        // All items in the tree will be scaled to these values for now.
        switch (role) {
        case R_TOTALSIZE:
            return QVariant(m_tree->subtreeSize());
        case R_MINAGE:
            return QVariant(m_chartsMin);
        case R_MAXAGE:
            return QVariant(m_chartsMax);
        case R_SIZE: {
            auto p = indexToDirTree(index);
            switch (p.second) {
            default:
            case IndexTarget::INVALID:
                return QVariant();
            case IndexTarget::ITSELF:
                return QVariant(p.first->subtreeSize());
            case IndexTarget::FILES:
                return QVariant(p.first->filesSize());
            }
        }
        case R_SORT: {
            switch (index.column()) {
            case C_NAME:
                return (indexToDirTree(index).second == IndexTarget::FILES) ?
                            QStringLiteral(u"\U0010FFFF") : data(index, Qt::DisplayRole);
            case C_TYPE:
                return data(index, Qt::DisplayRole);
            case C_SIZE:
                return data(index, R_SIZE);
            case C_MEDIAN_AGE:
            case C_AGE: {
                auto i = m_charts.find(index.siblingAtColumn(0));
                return i != m_charts.end() ? QVariant(i->median) : QVariant(HIGH);
            }
            default:
                return QVariant();
            }
        }
        default:
            return QVariant();
        }
    }
    else if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
        case C_SIZE:
        case C_MEDIAN_AGE:
            return QVariant(int(Qt::AlignRight | Qt::AlignVCenter));
        default:
            return QVariant();
        }
    }
    else if (role == Qt::DisplayRole) {
        auto p = indexToDirTree(index);
        auto chartsLookupFuzzy = [this](const QModelIndex &index) {
            auto i = m_charts.find(index.siblingAtColumn(0));
            if (i != m_charts.end())
                return QVariant(fuzzyDuration(i->median, m_resetTime));
            else
                return QVariant();
        };
        auto chartsLookupChart = [this](const QModelIndex &index) {
            auto i = m_charts.find(index.siblingAtColumn(0));
            if (i != m_charts.end())
                return QVariant::fromValue(*i);
            else
                return QVariant();
        };
        if (p.second == IndexTarget::ITSELF) {
            switch (index.column()) {
            default:
                return QVariant();
            case C_NAME:
                return QVariant(p.first->name());
            case C_TYPE:
                return QVariant(T_SUBDIR);
            case C_SIZE:
                return QVariant(displayFileSize(p.first->subtreeSize()));
            case C_MEDIAN_AGE:
                return chartsLookupFuzzy(index);
            case C_AGE:
                return chartsLookupChart(index);
            }
        }
        else if (p.second == IndexTarget::FILES) {
            switch (index.column()) {
            default:
                return QVariant();
            case C_NAME:
                return QVariant(QStringLiteral("[Files]"));
            case C_TYPE:
                return QVariant(T_FILE);
            case C_SIZE:
                return QVariant(displayFileSize(p.first->filesSize()));
            case C_MEDIAN_AGE:
                return chartsLookupFuzzy(index);
            case C_AGE:
                return chartsLookupChart(index);
            }
        }
        else {
            return QVariant();
        }
    }
    else {
        return QVariant();
    }
}
