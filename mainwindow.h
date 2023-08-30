
/**
 * This file is part of dirage2.
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "controller.h"
#include "dirmodel.h"
#include "agechartitemdelegate.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow final : public QMainWindow
{
    Q_OBJECT

    Controller*                 m_controller;
    DirModel*                   m_dirModel;
    QSortFilterProxyModel*      m_sortProxy;
    SearchService::Mode         m_searchMode;
    QTimer                      m_searchDebouncer;

    std::optional<QString>      m_lastScanMessage;
    int                         m_lastNumSearchResults;
    QModelIndex                 m_lastSelected;

public:
    MainWindow(Controller *controller, DirModel *model, QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onScanStateChanged(bool active);
    void onScanStatusMessage(QString message);
    void onViewSelectionChanged(const QModelIndex &now, const QModelIndex &prev);
    void onContextMenuRequest(QPoint point);
    void onSearchDone(int resultCount);

private:
    Ui::MainWindow *ui;
    void updateStatusMessage();

protected:
    virtual void changeEvent(QEvent *ev) override;

signals:
    void paletteChanged();
    void setScaling(AgeChartItemDelegate::Scaling scaling);
};
#endif // MAINWINDOW_H
