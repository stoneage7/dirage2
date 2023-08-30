
/**
 * This file is part of dirage2.
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#include <QPromise>
#include <QQueue>
#include <QSharedPointer>
#include <bit>
#include "searchservice.h"
#include "dirtree.h"

class SearchWorker;
struct SearchServicePrivate
{
    // Worker thread state.
    QThreadPool                     threadPool;
    QList<SearchWorker*>            workers;
    QAtomicInt                      busyCounter = 0;
    QAtomicInt                      exitCounter = 0;
    QPromise<DirTree*>*             promise = nullptr;

    void cancel()
    {
        if (promise != nullptr) {
            auto fut = promise->future();
            fut.cancel();
            fut.waitForFinished();
            delete promise;
            promise = nullptr;
        }
        busyCounter = 0;
        exitCounter = 0;
    }

    ~SearchServicePrivate()
    {
        cancel();
    }
};

// Worker threads.

class SplitMixRNG
{
public:
    SplitMixRNG(quint64 seed): m_state(seed)
    { }

    quint64 rand()
    {
        quint64 z = (m_state += 0x9E3779B97F4A7C15);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EB;
        return z ^ (z >> 31);
    }

private:
    quint64 m_state;
};

class SearchWorker: public QRunnable  // TODO needs a better queue.
{
public:
    //! Num is the order of this worker.
    SearchWorker(qsizetype num, SearchServicePrivate *shared, DirTree *tree):
        m_rng(static_cast<std::make_unsigned<qsizetype>::type>(num)),
        m_num(num),
        m_shared(shared)
    {
        m_queue.reserve(32);
        if (tree != nullptr) {
            m_queue.push_back(tree);
            m_shared->busyCounter.fetchAndAddOrdered(1);
        }
    }

    virtual void run() override
    {
        while (true) {
            lock();
            if (m_shared->promise->isCanceled()) {
                unlock();
                gracefulEnd();
                return;
            }
            else if (!m_queue.empty()) {
                DirTree *t = m_queue.dequeue();
                unlock();
                process(t);
                processChildren(t);
                lock();
                if (m_queue.empty()) {
                    m_shared->busyCounter.fetchAndSubRelaxed(1);
                }
                unlock();
            }
            else {
                unlock();
                std::make_unsigned<qsizetype>::type s = m_shared->workers.length();
                qsizetype victim;
                do {
                    victim = m_rng.rand() % s;
                } while (victim == m_num);
                DirTree *t = stealFrom(victim);
                if (t != nullptr) {
                    process(t);
                    processChildren(t);
                    lock();
                    if (m_queue.empty()) {
                        m_shared->busyCounter.fetchAndSubRelaxed(1);
                    }
                    unlock();
                }
                else {
                    int busy = m_shared->busyCounter.loadAcquire();
                    if (busy == 0) {
                        gracefulEnd();
                        return;
                    }
                    else {
                        QThread::yieldCurrentThread();
                    }
                }
            }
        }
    }

    DirTree *stealFrom(SearchWorker *thief)
    {
        lock();
        // Do not steal the victim's last item as that would not make more workers busy.
        if (m_queue.size() > 1) {
            auto *p = m_queue.dequeue();
            m_shared->busyCounter.fetchAndAddRelaxed(1);
            unlock();
            return p;
        } else {
            unlock();
            return nullptr;
        }
    }

    DirTree *stealFrom(int num)
    {
        return m_shared->workers.at(num)->stealFrom(this);
    }

    virtual void setSearchParam(const QString &s) = 0;

protected:
    virtual void process(DirTree *tree) = 0;

    void addResult(DirTree *tree)
    {
        lock();
        m_shared->promise->addResult(tree);
        unlock();
    }

private:
    void processChildren(DirTree *tree)
    {
        size_t n = tree->numChildren();
        for (size_t i = 0; i < n; ++i) {
            lock();
            if (m_queue.size() < m_queue.capacity()) {
                m_queue.enqueue(tree->child(i));
                unlock();
            }
            else {
                unlock();
                process(tree->child(i));
                processChildren(tree->child(i));
            }
        }
    }

    void lock()
    { while (!m_mutex.testAndSetAcquire(0, 1)) { } }

    void unlock()
    { m_mutex.storeRelease(0); }

    void gracefulEnd()
    {
        if (m_shared->exitCounter.fetchAndSubRelaxed(1) == 1) {
            m_shared->promise->finish();
        }
    }

    QQueue<DirTree*>        m_queue;
    QAtomicInt              m_mutex;
    SplitMixRNG             m_rng;
    SearchServicePrivate*   m_shared;
    int                     m_num;
};

class FixedSearchWorker: public SearchWorker
{
public:
    using SearchWorker::SearchWorker;

    virtual void setSearchParam(const QString &s)
    {
        m_searchString = s;
    }

protected:
    virtual void process(DirTree *tree)
    {
        if (tree->name().contains(m_searchString, Qt::CaseInsensitive))
            addResult(tree);
    }

private:
    QString                 m_searchString;
};

class WildcardSearchWorker: public SearchWorker
{
public:
    using SearchWorker::SearchWorker;

    virtual void setSearchParam(const QString &s)
    {
        m_regex = QRegularExpression::fromWildcard(s);
    }

protected:
    virtual void process(DirTree *tree)
    {
        if (m_regex.match(tree->name()).hasMatch()) {
            addResult(tree);
        }
    }

private:
    QRegularExpression      m_regex;
};

class RegexSearchWorker: public SearchWorker
{
public:
    using SearchWorker::SearchWorker;

    virtual void setSearchParam(const QString &s)
    {
        m_regex = QRegularExpression(s, QRegularExpression::CaseInsensitiveOption);
    }

protected:
    virtual void process(DirTree *tree)
    {
        if (m_regex.match(tree->name()).hasMatch()) {
            addResult(tree);
        }
    }

private:
    QRegularExpression      m_regex;
};

// Main thread.

SearchService::SearchService(QObject *parent):
    QObject(parent)
{
    p = new SearchServicePrivate;
    p->threadPool.setObjectName("SearchThreadPool");
}

SearchService::~SearchService()
{
    delete p;
}

QFuture<DirTree*> SearchService::start(const QString &str, DirTree *tree, Mode mode)
{
    cancel();
    p->promise = new QPromise<DirTree*>();
    p->promise->start();
    int numThreads = p->threadPool.maxThreadCount();
    p->workers.reserve(numThreads);
    p->workers.resize(numThreads);
    p->exitCounter.storeRelaxed(numThreads);
    for (int i = 0; i < numThreads; i++) {
        SearchWorker *w = nullptr;
        DirTree *t = (i == 0 ? tree : nullptr);
        switch (mode) {
        case Mode::FIXED:
            w = new FixedSearchWorker(i, p, t);
            break;
        case Mode::WILDCARD:
            w = new WildcardSearchWorker(i, p, t);
            break;
        case Mode::REGEX:
            w = new RegexSearchWorker(i, p, t);
            break;
        }
        w->setSearchParam(str);
        p->workers[i] = w;
        w->setAutoDelete(true);
    }
    for (int i = 0; i < numThreads; i++) {
        p->threadPool.start(p->workers.at(i));
    }
    return p->promise->future();
}

void SearchService::cancel()
{
    p->cancel();
}

