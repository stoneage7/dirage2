
/**
 * This file is part of dirage2.
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <QSet>
#include <functional>
#include "dirmodel.h"
#include "chartcalculatorservice.h"
#include "scannerservice.h"
#include "searchservice.h"
#include "savereportservice.h"

class Controller final : public QObject
{
    Q_OBJECT
public:
    explicit Controller(DirModel *model, QObject *parent = nullptr);
    ~Controller();

    using ModelIndexConsumer = std::function<void(const QModelIndex&)>;

signals:
    void scanStateChanged(bool active);
    void scanStatusMessage(QString msg);
    void searchDone(int numResults);
    void searchNeedsExpanding(QModelIndex index);
    void cancelReport();

public slots:
    void onOpenDirAction();
    void onCancelScanAction();
    void onRescanAction();
    void onTreeExpanded(QModelIndex index);
    void onOpenFromViewAction(QModelIndex index);
    void onOpenFromViewInFMAction(QModelIndex index);
    void onSearch(QString string, SearchService::Mode mode);
    void onProxyOrderChanged(QAbstractProxyModel *proxy);
    void onNextSearchResult(QModelIndex from, ModelIndexConsumer scrollFunc);
    void onPreviousSearchResult(QModelIndex from, ModelIndexConsumer scrollFunc);
    void onSaveReportAction();

private slots:
    void onDirChosen(QString dir);
    void onRequestCalculation(QModelIndex index);

private:
    void clearSearchResults();
    QModelIndex findInSearchResults(const QModelIndex &from, bool backwards);

    std::optional<QString>  m_currentRoot;
    DirModel*               m_model;
    QAbstractProxyModel*    m_proxyModel;
    ChartCalculatorService  m_chartCalculator;
    ScannerService          m_scanner;
    SaveReportService       m_reportService;

    SearchService           m_search;
    QSet<QModelIndex>       m_searchResultsProxied;
    QSet<QModelIndex>       m_searchResultsSource;
    QFuture<void>           m_searchFuture;
    QString                 m_searchString;
};

#endif // CONTROLLER_H
