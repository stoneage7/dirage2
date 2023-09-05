
/**
 * This file is part of dirage2.
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#include <QFileDialog>
#include <QFuture>
#include <QDesktopServices>
#include <QAbstractItemView>
#include <QEventLoop>
#include <QFileDialog>
#include <QProgressDialog>
#include <QMessageBox>

#include "controller.h"

// Utility functions

static void fullPath(QString &path, DirTree *tree)
{
    if (tree->parent() != nullptr) {
        fullPath(path, tree->parent());
        path += QDir::separator();
        path += tree->name();
    }
    else {
        path = tree->name();
    }
}

static QModelIndex next(const QAbstractItemModel *model, QModelIndex index)
{
    if (model->rowCount(index) > 0) {
        return model->index(0, 0, index);
    }
    else {
        while (index.isValid()) {
            QModelIndex parent = model->parent(index);
            if (index.row() + 1 < model->rowCount(parent)) {
                return model->index(index.row() + 1, 0, parent);
            }
            index = parent;
        }
    }
    return QModelIndex{};
}

static QModelIndex previous(const QAbstractItemModel *model, QModelIndex index)
{
    if (index.isValid()) {
        if (index.row() > 0) {
            index = model->index(index.row() - 1, 0, model->parent(index));
            int numChildren = model->rowCount(index);
            while (numChildren > 0) {
                index = model->index(numChildren - 1, 0, index);
                numChildren = model->rowCount(index);
            }
            return index;
        }
        else {
            return model->parent(index);
        }
    }
    else {
        return QModelIndex{};
    }
}

template <class T>
static const T *nvl2(const void *ptr, const T *ifNotNull, const T *ifNull)
{
    return (ptr != nullptr) ? ifNotNull : ifNull;
}

static QModelIndex whileCond(const QModelIndex &from,
                             const std::function<bool(const QModelIndex&)> &whileCond,
                             const std::function<bool(const QModelIndex&)> &pred,
                             const std::function<QModelIndex(const QModelIndex&)> &advance)
{
    QModelIndex now = from;
    while (whileCond(now)) {
        if (pred(now))
            return now;
        now = advance(now);
    }
    return {};
}

// Controller

Controller::Controller(DirModel *model, QObject *parent):
    QObject{parent},
    m_model(model)
{
    m_proxyModel = nullptr;
}

Controller::~Controller()
{
    emit cancelReport();
    m_scanner.cancel();
    m_chartCalculator.cancelAll();
}

void Controller::onRequestCalculation(QModelIndex index)
{
    if (index.isValid()) {
        auto [subtree, target] = m_model->indexToDirTree(index);
        QFuture<AgeChart> fut;
        if (target == DirModel::IndexTarget::FILES) {
            fut = m_chartCalculator.calculateFiles(subtree);
        }
        else if (target == DirModel::IndexTarget::ITSELF) {
            fut = m_chartCalculator.calculateSubtree(subtree);
        }
        else {
            return;
        }
        fut.then(this, [this, index](AgeChart chart) {
            m_model->calculated(index, chart);
        });
    }
}

void Controller::clearSearchResults()
{
    m_searchResultsSource.clear();
    m_searchResultsProxied.clear();
}

QModelIndex Controller::findInSearchResults(const QModelIndex &from, bool backwards)
{
    const auto *model = nvl2<QAbstractItemModel>(m_proxyModel, m_proxyModel, m_model);
    const auto *results = nvl2(m_proxyModel, &m_searchResultsProxied, &m_searchResultsSource);

    if (model->rowCount() == 0)
        return QModelIndex{};

    auto pred = [results](const QModelIndex &_i) { return results->contains(_i); };
    auto isValid = [](const QModelIndex &_i) { return _i.isValid(); };
    auto isNotStarting = [from](const QModelIndex &_i) { return  _i.isValid() && _i != from; };
    std::function<QModelIndex(const QModelIndex&)> advance;
    if (backwards)
        advance = [model](const QModelIndex &_i) { return previous(model, _i); };
    else
        advance = [model](const QModelIndex &_i) { return next(model, _i); };

    QModelIndex result = whileCond(advance(from), isValid, pred, advance);
    // If no result, then wrap search from start/end.
    if (!result.isValid()) {
        QModelIndex wrapped = model->index(0, 0);
        if (backwards) {
            int numChildren = model->rowCount(wrapped);
            while (numChildren > 0) {
                wrapped = model->index(numChildren - 1, 0, wrapped);
                numChildren = model->rowCount(wrapped);
            }
        }
        result = whileCond(wrapped, isNotStarting, pred, advance);
    }
    return result.isValid() ? result : QModelIndex{};
}

void Controller::onOpenDirAction()
{
    QString dir = QFileDialog::getExistingDirectory(nullptr, "Select a directory.");
    if (!dir.isEmpty())
        emit onDirChosen(dir);
}

void Controller::onDirChosen(QString dir)
{
    auto state = m_scanner.start(dir);
    QTimer *tmr =new QTimer(this);
    tmr->setInterval(1000);
    connect(tmr, &QTimer::timeout, this, [this, tmr, state]() {
        if (!state.future().isFinished()) {
            auto s = state.get();
            auto msg = QStringLiteral("Scanning... %1 files and %2 directiories. "
                                      "%3 skipped and %4 errors.")
                                      .arg(s.numFiles).arg(s.numDirs)
                                      .arg(s.numSkipped).arg(s.numErrors);
            emit scanStatusMessage(msg);
        }
        else {
            tmr->deleteLater();
        }
    });
    tmr->start();
    emit scanStateChanged(true);
    auto fut = state.future();
    fut.then(this, [this, dir](DirTree *tree) {
        m_model->reset(tree);
        emit scanStateChanged(false);
        if (m_model->rowCount() > 0) {
            emit cancelReport();
            m_chartCalculator.cancelAll();
            m_search.cancel();
            clearSearchResults();
            onRequestCalculation(m_model->index(0, 0));
            m_currentRoot = dir;
        }
        else {
            m_currentRoot.reset();
        }
    }).onCanceled(this, [this]() {
        emit scanStateChanged(false);
    });
}

void Controller::onCancelScanAction()
{
    m_scanner.cancel();
    emit scanStateChanged(false);
}

void Controller::onRescanAction()
{
    if (m_currentRoot.has_value()) {
        onDirChosen(m_currentRoot.value());
    }
}

void Controller::onTreeExpanded(QModelIndex index)
{
    for (int i = 0; i < m_model->rowCount(index); ++i) {
        QModelIndex child = m_model->index(i, index.column(), index);
        if (!m_model->isChartCached(child))
            onRequestCalculation(child);
    }
}

void Controller::onOpenFromViewAction(QModelIndex index)
{
    auto p = m_model->indexToDirTree(index);
    if (p.second != DirModel::IndexTarget::ITSELF)
        return;
    DirTree *tree = p.first;
    QString newPath;
    fullPath(newPath, tree);
    onDirChosen(newPath);
}

void Controller::onOpenFromViewInFMAction(QModelIndex index)
{
    auto p = m_model->indexToDirTree(index);
    if (p.second != DirModel::IndexTarget::ITSELF)
        return;
    DirTree *tree = p.first;
    QString newPath;
    fullPath(newPath, tree);
    QDesktopServices::openUrl(QUrl::fromLocalFile(newPath));
}

void Controller::onSearch(QString string, SearchService::Mode mode)
{
    if (string.isEmpty()) {
        m_searchString = string;
        clearSearchResults();
        emit searchDone(-1);
        return;
    }
    if (string == m_searchString) {
        return;
    }
    clearSearchResults();
    m_searchString = string;
    QModelIndex rootIndex = m_model->index(0, 0, QModelIndex());
    if (m_model->rowCount() > 0 && !string.isEmpty()) {
        auto [tree, target] = m_model->indexToDirTree(rootIndex);
        Q_ASSERT(target == DirModel::IndexTarget::ITSELF);
        auto fut = m_search.start(string, tree, mode);
        m_searchFuture = fut.then(this, [this](QFuture<DirTree*> fut) {
            for (int i = 0; i < fut.resultCount(); ++i) {
                QModelIndex sourceIndex = m_model->dirTreeToIndex(fut.resultAt(i));
                m_searchResultsSource.insert(sourceIndex);
                if (m_proxyModel != nullptr) {
                    QModelIndex proxiedIndex = m_proxyModel->mapFromSource(sourceIndex);
                    m_searchResultsProxied.insert(proxiedIndex);
                }
            }
            emit searchDone(fut.resultCount());
        });
    }
    else {
        m_search.cancel();
    }
}

void Controller::onProxyOrderChanged(QAbstractProxyModel *proxy)
{
    m_proxyModel = proxy;
    m_searchResultsProxied.clear();
    if (proxy != nullptr) {
        for (auto i = m_searchResultsSource.begin(); i != m_searchResultsSource.end(); ++i) {
            QModelIndex proxiedIndex = m_proxyModel->mapFromSource(*i);
            m_searchResultsProxied.insert(proxiedIndex);
        }
    }
}

void Controller::onNextSearchResult(QModelIndex from, ModelIndexConsumer scrollFunc)
{
    // Cannot simply do this because the continuation needs to run on the main thread as well.
    //m_searchFuture.waitForFinished();
    QEventLoop l;
    while (!m_searchFuture.isFinished()) {
        l.processEvents(QEventLoop::ExcludeUserInputEvents | QEventLoop::WaitForMoreEvents);
    }
    auto result = findInSearchResults(from, false);
    if (result.isValid())
        scrollFunc(result);
}

void Controller::onPreviousSearchResult(QModelIndex from, ModelIndexConsumer scrollFunc)
{
    // Cannot simply do this because the continuation needs to run on the main thread as well.
    //m_searchFuture.waitForFinished();
    QEventLoop l;
    while (!m_searchFuture.isFinished()) {
        l.processEvents(QEventLoop::ExcludeUserInputEvents | QEventLoop::WaitForMoreEvents);
    }
    auto result = findInSearchResults(from, true);
    if (result.isValid())
        scrollFunc(result);
}

void Controller::onSaveReportAction()
{
    using T = SaveReportService::ReportPtr;
    QString fileName = QFileDialog::getSaveFileName(nullptr, "Save File", "", "All Files (*)");
    if (fileName.isEmpty())
        return;
    DirTree *tree = m_model->indexToDirTree(m_model->index(0, 0)).first;
    QFuture<T> fut;
    fut = m_reportService.generateReport(&m_chartCalculator, tree);
    QProgressDialog progDlg("Generating report...", "Cancel", 0, 0);
    progDlg.setWindowModality(Qt::WindowModal);
    progDlg.setMinimumDuration(0);
    QFutureWatcher<T> watcher;
    connect(&progDlg, &QProgressDialog::canceled, &watcher, &QFutureWatcher<T>::cancel);
    connect(this, &Controller::cancelReport, &watcher, &QFutureWatcher<T>::cancel);
    watcher.setFuture(fut);
    progDlg.show();
    QEventLoop l;
    while (!fut.isFinished())
        l.processEvents(QEventLoop::WaitForMoreEvents);
    if (fut.isResultReadyAt(0)) {
        auto [rv, err] = m_reportService.saveReport(fut.result(), fileName);
        if (rv != QFileDevice::NoError) {
            QMessageBox::critical(nullptr, "Error", "Error saving report:" + err);
        }
    }
}
