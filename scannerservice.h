
/**
 * This file is part of dirage2.
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#ifndef SCANNERSERVICE_H
#define SCANNERSERVICE_H

#include <QObject>
#include <QFuture>
#include "dirtree.h"

class ScannerService final: public QObject
{
    Q_OBJECT
public:
    struct Progress
    {
        int numFiles = 0;
        int numDirs = 0;
        int numSkipped = 0;
        int numErrors = 0;
    };

    class State
    {
    public:
        State();
        QFuture<DirTree*> future() const;
        Progress get() const;
        struct Private;
    private:
        QSharedPointer<Private> p;
        friend class ScannerService;
    };

    ScannerService();
    ~ScannerService();

    bool isScanning() const;
    State start(QString dir);
    void cancel();

private:
    struct Private;
    Private *p;
};

#endif // SCANNERSERVICE_H
