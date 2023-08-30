
/**
 * This file is part of dirage2.
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#ifndef CHARTCALCULATORSERVICE_H
#define CHARTCALCULATORSERVICE_H

#include <QObject>
#include <QFuture>
#include "agechart.h"
#include "dirtree.h"

// A calculator service for the Controller.

class ChartCalculatorServicePrivate;
class ChartCalculatorService final: public QObject
{
    Q_OBJECT
public:
    ChartCalculatorService();
    ~ChartCalculatorService();

    //! Run a calculation in a thread pool.
    QFuture<AgeChart> calculateSubtree(DirTree *tree);
    QFuture<AgeChart> calculateFiles(DirTree *tree);

    //! Cancel all running futures, wait for them to finish.
    void cancelAll();

private:
    ChartCalculatorServicePrivate *p;
};

#endif // CHARTCALCULATORSERVICE_H
