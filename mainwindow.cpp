
/**
 * This file is part of dirage2.
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#include <QActionGroup>
#include <QToolButton>
#include <QLineEdit>
#include <QShortcut>
#include <functional>
#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include "agechartitemdelegate.h"

static void topDown(const QAbstractItemModel *model,
                    const QModelIndex &from,
                    const std::function<void(const QModelIndex&)> &consumer)
{
    QModelIndex parent = model->parent(from);
    if (parent.isValid())
        topDown(model, parent, consumer);
    consumer(from);
}

static void forSubtree(const QAbstractItemModel *model,
                       const QModelIndex &from,
                       const std::function<void(const QModelIndex &)> &consumer)
{
    consumer(from);
    int numChildren = model->rowCount(from);
    for (int j = 0; j < numChildren; ++j) {
        QModelIndex ch = model->index(j, 0, from);
        forSubtree(model, ch, consumer);
    }
}

static void forSiblings(const QAbstractItemModel *model,
                        const QModelIndex &from,
                        const std::function<void(const QModelIndex &)> &consumer)
{
    QModelIndex parent = model->parent(from);
    int numChildren = model->rowCount(parent);
    int currentRow = from.row();
    for (int j = 0; j < numChildren; ++j) {
        if (j != currentRow) consumer(model->index(j, 0, parent));
    }
}

MainWindow::MainWindow(Controller *controller, DirModel *model, QWidget *parent):
    QMainWindow(parent),
    m_controller(controller),
    m_dirModel(model),
    ui(new Ui::MainWindow),
    m_lastNumSearchResults(-1)
{
    ui->setupUi(this);
    connect(ui->actionOpen, &QAction::triggered, controller, &Controller::onOpenDirAction);
    connect(ui->actionCancel, &QAction::triggered, controller, &Controller::onCancelScanAction);
    connect(ui->actionRescan, &QAction::triggered, controller, &Controller::onRescanAction);
    onScanStateChanged(false);

    // Chart scaling.
    QToolButton *scaleButton = new QToolButton(this);
    scaleButton->setText("Scale");
    scaleButton->setPopupMode(QToolButton::InstantPopup);
    scaleButton->setIcon(QIcon::fromTheme("sqrt"));
    scaleButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    QMenu *scaleMenu = new QMenu("Scale", this);
    QActionGroup *scaleGroup = new QActionGroup(this);
    scaleGroup->addAction(ui->actionScaleLinear);
    scaleGroup->addAction(ui->actionScaleSqrt);
    scaleGroup->addAction(ui->actionScaleLn);
    scaleMenu->addActions(scaleGroup->actions());
    scaleButton->setMenu(scaleMenu);
    ui->toolBar->addWidget(scaleButton);
    connect(ui->actionScaleLinear, &QAction::triggered, this, [this]() {
        emit setScaling(AgeChartItemDelegate::Scaling::S_LINEAR);
        ui->treeView->viewport()->update();
    });
    connect(ui->actionScaleSqrt, &QAction::triggered, this, [this]() {
        emit setScaling(AgeChartItemDelegate::Scaling::S_SQRT);
        ui->treeView->viewport()->update();
    });
    connect(ui->actionScaleLn, &QAction::triggered, this, [this]() {
        emit setScaling(AgeChartItemDelegate::Scaling::S_LN);
        ui->treeView->viewport()->update();
    });

    // Search bar.
    QLineEdit *searchBar = new QLineEdit(this);
    searchBar->setPlaceholderText("Search... (Ctrl+F)");
    ui->toolBar->addWidget(searchBar);
    QShortcut *kbCtrlF = new QShortcut(QKeySequence("Ctrl+F"), this);
    connect(kbCtrlF, &QShortcut::activated, this, [searchBar]() {
        searchBar->setFocus();
        searchBar->selectAll();
    });
    QActionGroup *searchTypeGroup = new QActionGroup(this);
    searchTypeGroup->addAction(ui->actionSearchFixed);
    searchTypeGroup->addAction(ui->actionSearchWildcard);
    searchTypeGroup->addAction(ui->actionSearchRegex);
    ui->toolBar->addAction(ui->actionSearchFixed);
    ui->toolBar->addAction(ui->actionSearchWildcard);
    ui->toolBar->addAction(ui->actionSearchRegex);
    connect(ui->actionSearchFixed, &QAction::triggered, this, [this]() {
        m_searchMode = SearchService::Mode::FIXED;
    });
    connect(ui->actionSearchWildcard, &QAction::triggered, this, [this]() {
        m_searchMode = SearchService::Mode::WILDCARD;
    });
    connect(ui->actionSearchRegex, &QAction::triggered, this, [this]() {
        m_searchMode = SearchService::Mode::REGEX;
    });
    ui->actionSearchFixed->trigger();

    m_searchDebouncer.setInterval(500);
    m_searchDebouncer.setSingleShot(true);
    connect(searchBar, &QLineEdit::textChanged, this, [this]() {
        m_searchDebouncer.stop();
        m_searchDebouncer.start();
    });
    connect(&m_searchDebouncer, &QTimer::timeout, this, [this, searchBar]() {
        m_controller->onSearch(searchBar->text(), m_searchMode);
    });
    connect(m_controller, &Controller::scanStateChanged, this, [this, searchBar]() {
        m_lastSelected = QModelIndex();
        m_searchDebouncer.stop();
        m_controller->onSearch(searchBar->text(), m_searchMode);
    });
    auto expandAndScroll = [this](const QModelIndex &index) {
        const QAbstractItemModel *model = index.model();
        QModelIndex parent = model->parent(index);
        if (parent.isValid()) {
            topDown(model, model->parent(index), [this](const QModelIndex &_i) {
                ui->treeView->expand(_i);
            });
        }
        ui->treeView->scrollTo(index);
        ui->treeView->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
    };
    auto searchNextLambda = [this, searchBar, expandAndScroll]() {
        m_controller->onSearch(searchBar->text(), m_searchMode);
        m_controller->onNextSearchResult(ui->treeView->currentIndex(), expandAndScroll);
    };
    auto searchPreviousLambda = [this, searchBar, expandAndScroll]() {
        m_controller->onSearch(searchBar->text(), m_searchMode);
        m_controller->onPreviousSearchResult(ui->treeView->currentIndex(), expandAndScroll);
    };
    connect(searchBar, &QLineEdit::returnPressed, this, searchNextLambda);
    connect(m_controller, &Controller::searchDone, this, &MainWindow::onSearchDone);

    ui->toolBar->addAction(ui->actionSearchNext);
    ui->actionSearchNext->setText("\u2193");
    ui->actionSearchNext->setToolTip("Find Next");
    ui->toolBar->addAction(ui->actionSearchPrevious);
    ui->actionSearchPrevious->setText("\u2191");
    ui->actionSearchPrevious->setToolTip("Find Previous");
    connect(ui->actionSearchNext, &QAction::triggered, this, searchNextLambda);
    connect(ui->actionSearchPrevious, &QAction::triggered, this, searchPreviousLambda);

    // Tree view expanding item.
    connect(ui->treeView, &QTreeView::expanded, this, [this](QModelIndex index) {
        QModelIndex sourceIndex{m_sortProxy->mapToSource(index)};
        m_controller->onTreeExpanded(sourceIndex);
    });

    // Tree view sorting.
    m_sortProxy = new QSortFilterProxyModel(this);
    m_sortProxy->setSourceModel(m_dirModel);
    m_sortProxy->setSortRole(DirModel::R_SORT);
    ui->treeView->setModel(m_sortProxy);
    ui->treeView->setSortingEnabled(true);
    connect(ui->treeView->header(), &QHeaderView::sortIndicatorChanged, this, [this]() {
        m_controller->onProxyOrderChanged(m_sortProxy);
    });
    m_controller->onProxyOrderChanged(m_sortProxy); // initial set up of proxy.

    // Tree view drawing.
    ui->treeView->setColumnWidth(DirModel::C_NAME, 400);

    auto ageChartDelegate = new AgeChartItemDelegate(ui->treeView);
    ui->treeView->setItemDelegateForColumn(DirModel::C_AGE, ageChartDelegate);
    ui->treeView->hideColumn(DirModel::C_TYPE);
    connect(this, &MainWindow::paletteChanged,
            ageChartDelegate, &AgeChartItemDelegate::calculateColors);
    connect(this, &MainWindow::setScaling,
            ageChartDelegate, &AgeChartItemDelegate::setScaling);
    ui->actionScaleSqrt->trigger();

    // Tree view context menu.
    ui->treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->treeView, &QTreeView::customContextMenuRequested,
            this, &MainWindow::onContextMenuRequest);

    connect(m_controller, &Controller::scanStateChanged, this, &MainWindow::onScanStateChanged);
    connect(m_controller, &Controller::scanStatusMessage, this, &MainWindow::onScanStatusMessage);

    // Tree view status message.
    connect(ui->treeView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &MainWindow::onViewSelectionChanged);

    // Save button.
    connect(ui->actionSaveReport, &QAction::triggered,
            m_controller, &Controller::onSaveReportAction);

    show();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::onScanStateChanged(bool active)
{
    if (!active) {
        m_lastScanMessage.reset();
        m_lastSelected = QModelIndex();
    }
    ui->actionRescan->setEnabled(!active && m_dirModel->rowCount() > 0);
    ui->actionSaveReport->setEnabled(!active && m_dirModel->rowCount() > 0);
    ui->actionCancel->setEnabled(active);
    updateStatusMessage();
}

void MainWindow::onScanStatusMessage(QString message)
{
    m_lastScanMessage = message;
    updateStatusMessage();
}

void MainWindow::onViewSelectionChanged(const QModelIndex &now, const QModelIndex &prev)
{
    m_lastSelected = now;
    updateStatusMessage();
}

void MainWindow::onContextMenuRequest(QPoint point)
{
    QModelIndex proxiedIndex = ui->treeView->indexAt(point);
    QModelIndex index = m_sortProxy->mapToSource(proxiedIndex);
    if (!index.isValid())
        return;
    QMenu m;
    auto p = m_dirModel->indexToDirTree(index);
    if (p.second == DirModel::IndexTarget::ITSELF) {
        m.addAction(ui->actionOpenFromView);
        m.addAction(ui->actionOpenFromViewInFM);
    }
    m.addAction(ui->actionExpandAll);
    m.addAction(ui->actionExpandCollapseSiblingsToLevel);
    if (!m.isEmpty()) {
        QAction *trigger = m.exec(ui->treeView->mapToGlobal(point));
        if (trigger == ui->actionOpenFromView) {
            m_controller->onOpenFromViewAction(index);
        }
        else if (trigger == ui->actionOpenFromViewInFM) {
            m_controller->onOpenFromViewInFMAction(index);
        }
        else if (trigger == ui->actionExpandAll) {
            forSubtree(proxiedIndex.model(), proxiedIndex, [this](const QModelIndex &_i) {
                ui->treeView->expand(_i);
            });
        }
        else if (trigger == ui->actionExpandCollapseSiblingsToLevel) {
            // Suppose that the current subtree is expanded and visible.
            forSiblings(proxiedIndex.model(), proxiedIndex, [this](const QModelIndex &_i) {
                ui->treeView->collapse(_i);
            });
        }
    }
}

void MainWindow::onSearchDone(int resultCount)
{
    m_lastNumSearchResults = resultCount;
    updateStatusMessage();
}

void MainWindow::updateStatusMessage()
{
    if (m_lastScanMessage.has_value()) {
        ui->statusbar->showMessage(m_lastScanMessage.value());
    }
    else {
        QString msg;
        if (m_lastNumSearchResults > 0) {
            msg.append(QStringLiteral("%1 search results. ").arg(m_lastNumSearchResults));
        }
        if (m_lastSelected.isValid()) {
            QModelIndex i = m_lastSelected.siblingAtColumn(DirModel::C_AGE);
            QVariant v = i.data();
            if (v.canConvert<AgeChart>()) {
                AgeChart c = v.value<AgeChart>();
                QDateTime min   = QDateTime::fromSecsSinceEpoch(c.min);
//                QDateTime p5    = QDateTime::fromSecsSinceEpoch(c.lowerWhisker);
                QDateTime p25   = QDateTime::fromSecsSinceEpoch(c.lowerQuartile);
                QDateTime p50   = QDateTime::fromSecsSinceEpoch(c.median);
                QDateTime p75   = QDateTime::fromSecsSinceEpoch(c.upperQuartile);
//                QDateTime p95   = QDateTime::fromSecsSinceEpoch(c.upperWhisker);
                QDateTime max   = QDateTime::fromSecsSinceEpoch(c.max);
                QString msg2 = QStringLiteral("(min) %1 (25) %2 (mid) %3 (75) %4 (max) %5").
                        arg(min.toString(Qt::ISODate)).
                        arg(p25.toString(Qt::ISODate)).
                        arg(p50.toString(Qt::ISODate)).
                        arg(p75.toString(Qt::ISODate)).
                        arg(max.toString(Qt::ISODate));
                msg.append(msg2);
            }
        }
        if (msg.isEmpty()) {
            msg = "Ready.";
        }
        ui->statusbar->showMessage(msg);
    }
}

void MainWindow::changeEvent(QEvent *ev)
{
    if (ev->type() == QEvent::PaletteChange)
        emit paletteChanged();
    QMainWindow::changeEvent(ev);
}



