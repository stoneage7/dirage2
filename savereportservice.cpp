
/**
 * This file is part of dirage2.
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#include <QJsonDocument>
#include <QRunnable>
#include <QThread>
#include "dirtree.h"
#include "savereportservice.h"
#include "chartcalculatorservice.h"

struct SaveReportService::Report
{
    QJsonObject obj;
};

class JsonReportGenerator: public QRunnable
{
    struct TmpFuts
    {
        QFuture<AgeChart> subtreeChart;
        QFuture<AgeChart> filesChart;
    };

public:
    JsonReportGenerator(ChartCalculatorService *serv,
                        DirTree *tree,
                        QPromise<SaveReportService::ReportPtr> &&pro)
    {
        m_calcServ = serv;
        m_tree = tree;
        m_promise = std::move(pro);
    }

    virtual void run() override
    {
        auto maybeObj = comp(m_tree);
        if (maybeObj.has_value()) {
            SaveReportService::ReportPtr rep{new SaveReportService::Report};
            rep->obj = std::move(maybeObj.value());
            m_promise.addResult(std::move(rep));
            m_promise.finish();
        }
        else {
            if (!m_promise.isCanceled()) {
                auto e = std::make_exception_ptr(std::runtime_error("Error generating JSON"));
                m_promise.setException(e);
            }
            m_promise.finish();
        }
    }

private:
    std::optional<QJsonObject> comp(DirTree *tree)
    {
        if (m_promise.isCanceled())
            return {};
        TmpFuts tmp = node(tree);
        std::optional<QJsonArray> chArr;
        if (tree->numChildren() > 0) {
            chArr = QJsonArray{};
            for (size_t i = 0; i < tree->numChildren(); ++i) {
                auto ch = comp(tree->child(i));
                if (ch.has_value() && !m_promise.isCanceled())
                    chArr.value().append(ch.value());
                else
                    return {};
            }
        }
        tmp.subtreeChart.waitForFinished();
        tmp.filesChart.waitForFinished();
        QJsonObject rv;
        rv["name"] = tree->name();
        rv["numFiles"] = static_cast<qint64>(tree->numFiles());
        rv["subtreeSize"] = tree->subtreeSize();
        rv["filesSize"] = tree->filesSize();
        if (tmp.subtreeChart.isResultReadyAt(0))
            rv["subtreeChart"] = chartToJson(tmp.subtreeChart.result());
        else
            return {};
        if (tmp.filesChart.isResultReadyAt(0))
            rv["filesChart"] = chartToJson(tmp.filesChart.result());
        else
            return {};
        if (chArr.has_value()) {
            rv["subdirs"] = chArr.value();
        }
        return rv;
    }

    TmpFuts node(DirTree *tree)
    {
        return {
            .subtreeChart = m_calcServ->calculateSubtree(tree),
            .filesChart = m_calcServ->calculateFiles(tree)
        };
    }

    QJsonArray chartToJson(const AgeChart &chart)
    {
        return QJsonArray{
            chart.min, chart.lowerWhisker, chart.lowerQuartile, chart.median,
            chart.upperQuartile, chart.upperWhisker, chart.max
        };
    }

    ChartCalculatorService*                 m_calcServ;
    DirTree*                                m_tree;
    QPromise<SaveReportService::ReportPtr>  m_promise;
};


SaveReportService::SaveReportService(QObject *parent)
    : QObject{parent}
{

}

QFuture<SaveReportService::ReportPtr>
SaveReportService::generateReport(ChartCalculatorService *serv, DirTree *tree)
{
    QPromise<SaveReportService::ReportPtr> pro;
    auto fut = pro.future();
    QRunnable *task = new JsonReportGenerator(serv, tree, std::move(pro));
    QThreadPool::globalInstance()->start(task);
    task->setAutoDelete(true);
    return fut;
}

QPair<QFileDevice::FileError, QString>
SaveReportService::saveReport(ReportPtr report, QString fileName)
{
    QJsonDocument doc;
    doc.setObject(report->obj);
    QFile file{fileName};
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return qMakePair(file.error(), file.errorString());
    if (file.write(doc.toJson()) < 0)
        return qMakePair(file.error(), file.errorString());
    return {};
}
