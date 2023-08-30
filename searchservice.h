
/**
 * This file is part of dirage2.
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#ifndef SEARCHSERVICE_H
#define SEARCHSERVICE_H

#include "dirtree.h"

struct SearchServicePrivate;
class SearchService : public QObject
{
    Q_OBJECT
public:
    explicit SearchService(QObject *parent = nullptr);
    ~SearchService();

    enum class Mode { FIXED, WILDCARD, REGEX };
    QFuture<DirTree*> start(const QString &str, DirTree *tree, Mode mode);
    void cancel();

signals:

private:
    SearchServicePrivate *p;
};

#endif // SEARCHSERVICE_H
