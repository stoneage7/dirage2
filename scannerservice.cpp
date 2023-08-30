
/**
 * This file is part of dirage2.
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#include <optional>
#include <stack>
#include <boost/smart_ptr/intrusive_ptr.hpp>
#include <boost/pool/object_pool.hpp>
#include "scannerservice.h"

// POSIX.
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

// Path element used by the scanning routine. Only supposed to be allocated via the pool.
struct _El;
typedef boost::intrusive_ptr<_El> _ElPtr;
typedef boost::object_pool<_El> _ElPool;

struct _El
{
    Q_DISABLE_COPY_MOVE(_El)
    _El() = default;
    std::string         name;
    _ElPtr              parent;
    _ElPool*            pool;
    int                 refs = 0;

    friend void intrusive_ptr_add_ref(_El* e);

    friend void intrusive_ptr_release(_El *e);
};

void intrusive_ptr_add_ref(_El *e)
{ ++e->refs; }

void intrusive_ptr_release(_El *e)
{ if (--e->refs == 0) e->pool->destroy(e); }

// Shared data structure of the scanning state.
struct ScannerService::State::Private
{
    Q_DISABLE_COPY_MOVE(Private)
    ScannerService::Progress    progress;
    QPromise<DirTree*>          promise;
    QPromise<void>              track;
    QAtomicInt                  mutex = 0;  // Only need to avoid a single read/write race.
    _ElPool                     elPool;  // One allocator per scan.

    Private()
    { }

    void lock()
    { while (!mutex.testAndSetAcquire(0, 1)) { }; }

    void unlock()
    { mutex.storeRelease(0); }

    void incrFiles()
    { lock(); ++progress.numFiles; unlock(); }

    void incrDirs()
    { lock(); ++progress.numDirs; unlock(); }

    void incrSkipped()
    { lock(); ++progress.numSkipped; unlock(); }

    void incrErrors()
    { lock(); ++progress.numErrors; unlock(); }

    _ElPtr allocElement(std::string &&name, _ElPtr parent)
    {
        _El *e = elPool.construct();
        e->pool = &elPool;
        e->name = std::move(name);
        e->parent = parent;
        return e;
    }
};

ScannerService::State::State():
    p{new Private}
{ }

QFuture<DirTree*> ScannerService::State::future() const
{
    return p->promise.future();
}

ScannerService::Progress ScannerService::State::get() const
{
    return p->progress;
}

// Worker QRunnable that does that actual scan.

class ScanWorker: public QRunnable
{
public:
    ScanWorker(QString path, QSharedPointer<ScannerService::State::Private> state):
        m_state{state}, m_rootPath{path}
    { m_state->promise.start(); }

    virtual void run()
    {
        auto &promise = m_state->promise;
        auto &track = m_state->track;
        DirTree *r = nullptr;
        try {
            r = scanPriv();
            if (r != nullptr) {
                promise.addResult(r);
                promise.finish();
                track.finish();
                return;
            }
            else {
                qDebug() << "scanPriv() returned null";
                promise.addResult(static_cast<DirTree*>(nullptr));
                promise.finish();
                track.finish();
                return;
            }
        }
        catch (const std::exception &e) {
            qWarning() << "ScanWorker::run(): error while scanning" << m_rootPath << ":"
                       << e.what();
            promise.setException(std::make_exception_ptr(e));
            track.finish();
            return;
        }
        catch (...) {
            qWarning() << "ScanWorker::run(): unknown error while scanning" << m_rootPath;
            promise.setException(std::make_exception_ptr(std::exception()));
            track.finish();
            return;
        }
    }

private:
    static void fullPath(_ElPtr el, std::string &buf, size_t totLen = 0)
    {
        static const char sep[] = {QDir::separator().toLatin1(), '\0'};
        if (el->parent == nullptr) {
            buf.reserve(totLen + el->name.size() + 1);
            buf.clear();
            buf.append(el->name);
            buf.append(sep);
        }
        else {
            fullPath(el->parent, buf, totLen + el->name.size() + 1);
            buf.append(el->name);
            buf.append(sep);
        }
    }

    DirTree *scanPriv()
    {
        std::unique_ptr<DirTree> root = std::make_unique<DirTree>();
        root->name(m_rootPath);

        std::stack<DirTree*> stack;
        stack.push(root.get());

        // Keeping a parallel string to skip encoding/decoding from QString.
        std::stack<_ElPtr> nameStack;
        nameStack.push(m_state->allocElement(m_rootPath.toStdString(), nullptr));

        while (!stack.empty()) {
            if (m_state->promise.isCanceled())
                return nullptr;

            DirTree *top = stack.top();
            stack.pop();
            _ElPtr topName = nameStack.top();
            nameStack.pop();
            std::string nameBuffer;

            // Reserve and copy the full path to a buffer.
            // The buffer's capacity never shrinks to avoid reallocs.
            fullPath(topName, nameBuffer, 0);
            DIR *dir = opendir(nameBuffer.c_str());
            if (dir == nullptr) {
                m_state->incrErrors();
                continue;
            }

            struct dirent *ent;
            size_t nameBufferLength = nameBuffer.size();

            // Breadth-first: read all children at this level before proceeding.
            while ((ent = readdir(dir)) != nullptr) {
                
                // Trim the path to parent's length.
                nameBuffer.resize(nameBufferLength);

                if (m_state->promise.isCanceled())
                    return nullptr;

                if (strncmp(ent->d_name, ".", sizeof(ent->d_name)) == 0 ||
                        strncmp(ent->d_name, "..", sizeof(ent->d_name)) == 0)
                    continue;

                // Append name of current child and stat().
                nameBuffer.append(ent->d_name);
                struct stat st;
                if (lstat(nameBuffer.c_str(), &st) == -1) {
                    m_state->incrErrors();
                    continue;
                }

                if (S_ISDIR(st.st_mode)) {
                    m_state->incrDirs();
                    DirTree *p = new DirTree();
                    p->name(QString(ent->d_name));
                    top->append(p);
                    stack.push(p);
                    nameStack.push(m_state->allocElement(ent->d_name, topName));
                }
                else if (S_ISREG(st.st_mode)) {
                    m_state->incrFiles();
                    top->append(st.st_size, st.st_mtime);
                }
                else {
                    // Symlinks and all other types are skipped.
                    m_state->incrSkipped();
                }
            }
            // Sort files at this level by timestamp and close the descriptor.
            top->finalize();
            closedir(dir);
        }
        return root.release();
    }

    QString                                         m_rootPath;
    QSharedPointer<ScannerService::State::Private>  m_state;
};


struct ScannerService::Private
{
    std::optional<ScannerService::State> currentScan;
};

ScannerService::ScannerService():
    p(new Private())
{
}

ScannerService::~ScannerService()
{
    delete p;
}

bool ScannerService::isScanning() const
{
    return p->currentScan.has_value();
}

ScannerService::State ScannerService::start(QString dir)
{
    cancel();
    State state;
    ScanWorker *task = new ScanWorker(dir, state.p);
    p->currentScan = state;
    auto fut = state.p->track.future();
    auto reset = [this]() { p->currentScan.reset(); };
    fut.then(this, reset).onCanceled(this, reset);
    task->setAutoDelete(true);
    QThreadPool::globalInstance()->start(task);
    return state;
}

void ScannerService::cancel()
{
    if (p->currentScan.has_value()) {
        State &state = p->currentScan.value();
        auto fut = state.future();
        fut.cancel();
        fut.waitForFinished();
    }
}


