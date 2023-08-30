
/**
 * This file is part of dirage2.
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 */

#include "dirtree.h"
#include <algorithm>
#include <mutex>
#include <boost/pool/pool_alloc.hpp>

constexpr size_t DIRTREE_INITIAL_FILE_VECTOR = 1024;
constexpr size_t DIRTREE_INITIAL_SUBS_VECTOR = 1024;

boost::pool_allocator<DirTree::iterator> g_iterAllocator;

static DirTree::iterator *allocateIterators(size_t n)
{
    return g_iterAllocator.allocate(n);
}

static void freeIterators(DirTree::iterator *iterators, size_t n)
{
    return g_iterAllocator.deallocate(iterators, n);
}

DirTree::DirTree() noexcept
{
    m_parent = nullptr;
    m_parentPos = 0;
    m_subtreeSize = 0;
    m_filesSize = 0;
}

DirTree::~DirTree()
{
    for (DirTree *ch : m_subdirs) {
        delete ch;
    }
}

void DirTree::append(file_size_t size, file_time_t time)
{
    if (m_files.size() == 0)
        m_files.reserve(DIRTREE_INITIAL_FILE_VECTOR);
    if (m_files.size() > 0 && m_files.back().time == time)
        m_files.back().size += size;
    else
        m_files.push_back(FileInfo{.size = size, .time = time});

    DirTree* p = this;
    p->m_filesSize += size;
    while (p != nullptr) {
        p->m_subtreeSize += size;
        p = p->m_parent;
    }
}

void DirTree::append(DirTree *subdir)
{
    if (m_subdirs.size() == 0) {
        m_subdirs.reserve(DIRTREE_INITIAL_SUBS_VECTOR);
    }
    m_subdirs.push_back(subdir);
    DirTree *b = m_subdirs.back();
    Q_ASSERT(b->m_parent == nullptr);
    b->m_parent = this;
    b->m_parentPos = m_subdirs.size() - 1;
    DirTree *p = this;
    while (p != nullptr) {
        p->m_subtreeSize += b->subtreeSize();
        p = p->m_parent;
    }
}

void DirTree::finalize()
{
    std::sort(m_files.begin(), m_files.end(),
              [](const FileInfo &a, const FileInfo &b) { return a.time < b.time; });
    m_subdirs.shrink_to_fit();
    m_files.shrink_to_fit();
}

DirTree *DirTree::child(size_t i)
{
    Q_ASSERT(i < m_subdirs.size());
    return m_subdirs[i];
}

static bool heap_comp_asc_time(const DirTree::iterator &a, const DirTree::iterator &b)
{
    return a->time > b->time;
}

void DirTree::iterator::_init(DirTree *tree)
{
    if (tree != nullptr && tree->numChildren() > 0) {
        m_subsCapacity = tree->numChildren();
        m_subs = allocateIterators(m_subsCapacity);
        // Allocate iterators of subdirs and put them on a heap, by time.
        for (auto i = m_tree->m_subdirs.begin(); i != m_tree->m_subdirs.end(); i++) {
            DirTree *j = *i;
            DirTree::iterator begin = j->begin();
            if (begin != j->end()) {
                // Construct in place at the back and push into the heap.
                new(m_subs + m_subsSize) DirTree::iterator(std::move(begin));
                m_subsSize++;
                std::push_heap(m_subs, m_subs + m_subsSize, &heap_comp_asc_time);
            }
        }
        // Get the first iterator.
        operator++();
    }
    else if (tree != nullptr) {
        // No subdirs, just advance.
        operator++();
    }
    else {
        // Nothing. Return end iterator.
        m_current = nullptr;
    }
}

void DirTree::iterator::_deinit()
{
    if (m_subs != nullptr) freeIterators(m_subs, m_subsCapacity);
}

DirTree::iterator::iterator(iterator &&other): iterator(nullptr)
{
    swap(std::move(other));
}

DirTree::iterator &DirTree::iterator::operator=(iterator &&other)
{
    if (this != &other) {
        swap(std::move(other));
    }
    return *this;
}

void DirTree::iterator::swap(iterator &&other)
{
    std::swap(m_tree, other.m_tree);
    std::swap(m_current, other.m_current);
    std::swap(m_pos, other.m_pos);
    std::swap(m_subs, other.m_subs);
    std::swap(m_subsSize, other.m_subsSize);
    std::swap(m_subsCapacity, other.m_subsCapacity);
}

DirTree::iterator::reference DirTree::iterator::operator*() const
{
    return *operator->();
}

DirTree::iterator::pointer DirTree::iterator::operator->() const
{
    return m_current;
}

DirTree::iterator &DirTree::iterator::operator++()
{
    // Go to next file in this dir, or subdirs, by ascending order, by time.
    if (m_subsSize > 0) {
        // Check if the next lowest time is a file or a subdir. Subdirs are in a heap.
        const iterator &best = (*m_subs);
        if (m_tree->m_files.size() > m_pos && m_tree->m_files[m_pos].time < best->time) {
            // Advance files pointer.
            m_current = &m_tree->m_files[m_pos];
            m_pos++;
        }
        else {
            // In a subdir.
            m_current = best.m_current;
            // Move the current item to the back of the vector.
            std::pop_heap(m_subs, m_subs + m_subsSize, &heap_comp_asc_time);
            // Advance that iterator.
            iterator &back = *(m_subs + m_subsSize - 1);
            ++back;
            // Move it back into the heap or delete if there's no more items.
            if (back != iterator(nullptr)) {
                std::push_heap(m_subs, m_subs + m_subsSize, &heap_comp_asc_time);
            }
            else {
                back.~iterator();
                m_subsSize--;
            }
        }
    }
    else {
        if (m_tree->m_files.size() > m_pos) {
            m_current = &m_tree->m_files[m_pos];
            m_pos++;
        }
        else {
            m_current = nullptr;
        }
    }
    return *this;
}

DirTree::iterator &DirTree::iterator::operator+=(int n)
{
    while (n > 0) {
        operator++();
        n--;
    }
    return *this;
}

bool DirTree::iterator::operator==(const iterator& other) const
{
    return (m_current == nullptr && other.m_current == nullptr) ||
            (m_tree == other.m_tree && m_current == other.m_current);
}

bool DirTree::iterator::operator!=(const iterator& other) const
{
    return !operator==(other);
}
