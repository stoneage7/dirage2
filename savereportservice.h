
/**
 * This file is part of dirage2.
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#ifndef SAVEREPORTSERVICE_H
#define SAVEREPORTSERVICE_H

#include <QObject>
#include <QFuture>
#include <QSharedPointer>
#include "chartcalculatorservice.h"

class SaveReportService : public QObject
{
    Q_OBJECT
public:
    explicit SaveReportService(QObject *parent = nullptr);

    struct Report;
    using ReportPtr = QSharedPointer<Report>;
    QFuture<ReportPtr> generateReport(ChartCalculatorService *serv, DirTree *tree);
    QPair<QFileDevice::FileError, QString> saveReport(ReportPtr report, QString fileName);

signals:

};

#endif // SAVEREPORTSERVICE_H
