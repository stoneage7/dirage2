
/**
 * This file is part of dirage2.
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#ifndef DIRTREE_H
#define DIRTREE_H

#include <QtCore>
#include <iterator>
#include <memory>
#include <vector>

class DirTree
{
public:
    Q_DISABLE_COPY_MOVE(DirTree)
    using file_size_t = qint64;
    using file_time_t = qint64;
    typedef struct { file_size_t size; file_time_t time; } FileInfo;

    DirTree() noexcept;
    ~DirTree();

    void append(file_size_t size, file_time_t time);
    void append(DirTree *subdir);
    void finalize();

    //! This should be const but since QModelIndex needs non-const void*, this is not const either.
    DirTree *child(size_t i);

    void name(const QString& name)
    { m_name = name; }

    const QString& name() const
    { return m_name; };

    size_t numChildren() const
    { return m_subdirs.size(); }

    size_t numFiles() const
    { return m_files.size(); }

    //! This should be const but since QModelIndex needs non-const void*, this is not const either.
    DirTree *parent()
    { return m_parent; }

    size_t parentPos() const
    { return m_parentPos; }

    file_size_t filesSize() const
    { return m_filesSize; }

    file_size_t subtreeSize() const
    { return m_subtreeSize; }

    const std::vector<FileInfo> &files() const
    { return m_files; }

    class iterator
    {
    private:
        DirTree*                m_tree;
        FileInfo*               m_current;
        iterator*               m_subs;
        size_t                  m_subsSize;
        size_t                  m_subsCapacity;
        int                     m_pos;
        void _init(DirTree*);
        void _deinit();


    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = FileInfo;
        using pointer = FileInfo*;
        using reference = FileInfo&;

        constexpr iterator(DirTree *tree):
            m_tree{tree},
            m_pos{0},
            m_subs{nullptr},
            m_subsSize{0},
            m_subsCapacity{0},
            m_current{nullptr}
        { if (tree != nullptr) _init(tree); }

        iterator(const iterator&) = delete;
        iterator(iterator&&);
        iterator &operator=(const iterator&) = delete;
        iterator &operator=(iterator&&);
        void swap(iterator&&);

        constexpr ~iterator()
        { if (m_subs != nullptr) _deinit(); }

        reference operator*() const;
        pointer operator->() const;
        iterator& operator++();
        iterator& operator+=(int);
        bool operator==(const iterator&) const;
        bool operator!=(const iterator&) const;
    };

    iterator begin()
    { return iterator(this); }

    iterator end()
    { return iterator(nullptr); }

private:
    QString                 m_name;
    std::vector<FileInfo>   m_files;
    std::vector<DirTree*>   m_subdirs;
    DirTree*                m_parent;
    size_t                  m_parentPos;
    file_size_t             m_filesSize;
    file_size_t             m_subtreeSize;
};

#endif // DIRTREE_H
