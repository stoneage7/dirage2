
/**
 * This file is part of dirage2.
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#include <QPromise>
#include <boost/intrusive/list.hpp>
#include <limits>
#include "chartcalculatorservice.h"

constexpr qint64 LOW = std::numeric_limits<decltype(AgeChart::min)>::lowest();

class ChartCalculatorServicePrivate final
{
private:
    class CalculationTaskBase:
            public QRunnable,
            public boost::intrusive::list_base_hook<>
    {
    public:
        CalculationTaskBase(QPromise<AgeChart> &&pro,
                            DirTree *tree,
                            QRecursiveMutex *lock,
                            boost::intrusive::list<CalculationTaskBase> *list):
            m_pro{std::move(pro)},
            m_tree{tree},
            m_lock{lock},
            m_list{list}
        {
            QMutexLocker l{lock};
            m_list->push_back(*this);
        }

        void cancel()
        {
            auto fut = m_pro.future();
            fut.cancel();
            fut.waitForFinished();
        }

        virtual void runPriv() = 0;

        virtual void run() override
        {
            runPriv();
            {
                QMutexLocker l{m_lock};
                auto i = m_list->iterator_to(*this);
                m_list->erase(i);
            }
        }
    protected:
        QPromise<AgeChart> m_pro;
        DirTree* m_tree;
        QRecursiveMutex* m_lock;
        boost::intrusive::list<CalculationTaskBase>* m_list;
    };

    using TaskList = boost::intrusive::list<CalculationTaskBase>;
    QRecursiveMutex         m_lock;
    TaskList                m_taskList;

    class SubtreeCalculationTask: public CalculationTaskBase
    {
    public:
        using CalculationTaskBase::CalculationTaskBase;

        virtual void runPriv() override
        {
            AgeChart ret;
            if (m_tree->subtreeSize() == 0) {
                m_pro.addResult(AgeChart());
                m_pro.finish();
                return;
            }
            qint64 accumulatedWeights = 0;
            qint64 totalWeight = m_tree->subtreeSize();
            qint64 lowerWhiskerWeight = totalWeight / 20;
            qint64 lowerQuartileWeight = totalWeight / 4;
            qint64 medianWeight = totalWeight / 2;
            qint64 upperQuartileWeight = totalWeight - (totalWeight / 4);
            qint64 upperWhiskerWeight = totalWeight - (totalWeight / 20);

            if (m_pro.isCanceled()) {
                m_pro.finish();
                return;
            }

            auto i = m_tree->begin();
            if (i != m_tree->end()) {
                ret.min = i->time;
            }
            int cnt = 0;
            for (; i != m_tree->end(); ++i) {
                cnt++;
                if (m_pro.isCanceled()) {
                    m_pro.finish();
                    return;
                }
                accumulatedWeights += i->size;
                if (ret.lowerWhisker == LOW && accumulatedWeights >= lowerWhiskerWeight) {
                    ret.lowerWhisker = i->time;
                }
                if (ret.lowerQuartile == LOW && accumulatedWeights >= lowerQuartileWeight) {
                    ret.lowerQuartile = i->time;
                }
                if (ret.median == LOW && accumulatedWeights >= medianWeight) {
                    ret.median = i->time;
                }
                if (ret.upperQuartile == LOW && accumulatedWeights >= upperQuartileWeight) {
                    ret.upperQuartile = i->time;
                }
                if (ret.upperWhisker == LOW && accumulatedWeights >= upperWhiskerWeight) {
                    ret.upperWhisker = i->time;
                }
                ret.max = i->time;
            }
            m_pro.addResult(ret);
            m_pro.finish();
        }
    };

    class FilesCalculationTask: public CalculationTaskBase
    {
    public:
        using CalculationTaskBase::CalculationTaskBase;

        virtual void runPriv() override
        {
            AgeChart ret;
            const std::vector<DirTree::FileInfo> &files = m_tree->files();
            if (files.size() == 0) {
                m_pro.addResult(AgeChart());
                m_pro.finish();
                return;
            }

            qint64 accumulatedWeights = 0;
            qint64 totalWeight = m_tree->filesSize();
            qint64 lowerWhiskerWeight = totalWeight / 20;
            qint64 lowerQuartileWeight = totalWeight / 4;
            qint64 medianWeight = totalWeight / 2;
            qint64 upperQuartileWeight = totalWeight - (totalWeight / 4);
            qint64 upperWhiskerWeight = totalWeight - (totalWeight / 20);

            if (m_pro.isCanceled()) {
                m_pro.finish();
                return;
            }

            auto i = files.begin();
            if (i != files.end()) {
                ret.min = i->time;
            }
            for (; i != files.end(); ++i) {
                if (m_pro.isCanceled()) {
                    m_pro.finish();
                    return;
                }
                accumulatedWeights += i->size;
                if (ret.lowerWhisker == LOW && accumulatedWeights >= lowerWhiskerWeight) {
                    ret.lowerWhisker = i->time;
                }
                if (ret.lowerQuartile == LOW && accumulatedWeights >= lowerQuartileWeight) {
                    ret.lowerQuartile = i->time;
                }
                if (ret.median == LOW && accumulatedWeights >= medianWeight) {
                    ret.median = i->time;
                }
                if (ret.upperQuartile == LOW && accumulatedWeights >= upperQuartileWeight) {
                    ret.upperQuartile = i->time;
                }
                if (ret.upperWhisker == LOW && accumulatedWeights >= upperWhiskerWeight) {
                    ret.upperWhisker = i->time;
                }
                ret.max = i->time;
            }
            m_pro.addResult(ret);
            m_pro.finish();
        }
    };

    template <class C>
    QFuture<AgeChart> calculate(DirTree *tree)
    {
        QMutexLocker l{&m_lock};
        QPromise<AgeChart> pro;
        pro.start();
        QFuture<AgeChart> fut = pro.future();
        QRunnable *runnable = new C{std::move(pro), tree, &m_lock, &m_taskList};
        runnable->setAutoDelete(true);
        QThreadPool::globalInstance()->start(runnable);
        return fut;
    }

public:
    ChartCalculatorServicePrivate()
    { }

    ~ChartCalculatorServicePrivate()
    {
        cancelAll();
    }

    QFuture<AgeChart> calculateSubtree(DirTree *tree)
    { return calculate<SubtreeCalculationTask>(tree); }

    QFuture<AgeChart> calculateFiles(DirTree *tree)
    { return calculate<FilesCalculationTask>(tree); }

    void cancelAll()
    {
        QMutexLocker l{&m_lock};
        while (!m_taskList.empty()) {
            auto iter = m_taskList.begin();
            iter->cancel();
        }
    }
};

ChartCalculatorService::ChartCalculatorService():
    p(new ChartCalculatorServicePrivate)
{

}

ChartCalculatorService::~ChartCalculatorService()
{
    cancelAll();
    delete p;
}

QFuture<AgeChart> ChartCalculatorService::calculateSubtree(DirTree *tree)
{
    return p->calculateSubtree(tree);
}

QFuture<AgeChart> ChartCalculatorService::calculateFiles(DirTree *tree)
{
    return p->calculateFiles(tree);
}

void ChartCalculatorService::cancelAll()
{
    p->cancelAll();
}

