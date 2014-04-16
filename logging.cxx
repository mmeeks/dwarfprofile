/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * a De-Dwarfe'd C++ microcosm:
 */

#include <vector>
#include <algorithm>
#include <malloc.h>
#include <assert.h>
#include <string.h>
#include <logging.hxx>

struct FileSystemNode;
struct FileSystemNode {
    char           *mpName;
    FileSystemNode *mpParent;

    typedef std::vector< FileSystemNode * > ChildsType; // Hamburg nostalgia
    ChildsType      maChildren;

    FileSystemNode (FileSystemNode *pParent,
                    const char *pName, int nLength)
    {
        mpName = strndup (pName, nLength);
        mpParent = pParent;
        if (mpParent)
            mpParent->maChildren.push_back(this);
        mnSize = 0;
        useCount = 0;
    }
    ~FileSystemNode()
    {
        free (mpName);
    }

    static FileSystemNode *gpRoot;

    static FileSystemNode *getNode (const char *pPath)
    {
        assert (pPath != NULL);
        if (!gpRoot)
            gpRoot = new FileSystemNode (NULL, "", 0);

        FileSystemNode *pNode = gpRoot;
        for (int last = 0, i = 0; pPath[i]; i++)
        {
            if (pPath[i] == '/')
            {
                if (i - last > 0)
                {
                    FileSystemNode *pChild;
                    pChild = pNode->lookupNode(pPath + last, i - last);
                    assert (pChild != NULL);
                    pNode = pChild;
                }
                last = i + 1;
            }
        }
        return pNode;
    }

    FileSystemNode *lookupNode (const char *pName, int nLength)
    {
        // Un-mess-up relative paths etc. hoping that
        // symlinks are kind to us.
        if (!strncmp (pName, "..", nLength))
            return mpParent ? mpParent : gpRoot;
        if (!strncmp (pName, ".", nLength))
            return this;

        // slow as you like etc.
        for (ChildsType::iterator it = maChildren.begin();
             it != maChildren.end(); ++it)
        {
            if (!strncmp ((*it)->mpName, pName, nLength))
                return *it;
        }
        return new FileSystemNode(this, pName, nLength);
    }

    // Payload
    size_t mnSize;

    size_t useCount;

    // Size accumulated down the tree
    void addSize (size_t nSize)
    {
        mnSize += nSize;
        useCount++;
        if (mpParent != NULL)
            mpParent->addSize (nSize);
    }

    static void accumulate_size (const char *pName, const char *pFunc,
                                 int line, int col, size_t size)
    {
        if (size == 0)
        {
// MJW - checkme - why is this zero so often ? ...
//            fprintf (stderr, "odd zero size at '%s' '%s'\n", pName, pFunc);
            return;
        }
        (void)line; (void)col; // later
        FileSystemNode *pNode = getNode(pName);
        if (pFunc)
            pNode = pNode->lookupNode(pFunc, strlen(pFunc));
        pNode->addSize (size);
    }

    void dumpAtDepth (int nDepth)
    {
        if (nDepth < 0)
            return;
        static const char aIndent[] = "|                ";
        assert (nDepth < (int)sizeof (aIndent));
        const char *pIndent = aIndent + nDepth;

        for (ChildsType::iterator it = maChildren.begin();
             it != maChildren.end(); ++it)
        {
            fprintf (stdout, "%10lu %8lu %s%s\n",
                     (unsigned long)(*it)->mnSize,
                     (*it)->useCount,
                     pIndent,
                     (*it)->mpName);
            (*it)->dumpAtDepth (nDepth-1);
        }
    }

    static bool big_first (FileSystemNode *a, FileSystemNode *b)
    {
        return a->mnSize > b->mnSize;
    }

    void sortChildren()
    {
        std::sort (maChildren.begin(), maChildren.end(), big_first);

        for (ChildsType::iterator it = maChildren.begin();
             it != maChildren.end(); ++it)
            (*it)->sortChildren();
    }
};

FileSystemNode *FileSystemNode::gpRoot = NULL;

void
register_compile_unit (const char *name, size_t size)
{
    fprintf (stdout, "compile-unit '%s', size: %ld\n",
             name, (long)size);
}

void register_file_span (const char *path, const char *func,
                         int line, int col, size_t size)
{
    if (!path)
        return;

    FileSystemNode::accumulate_size (path, func, line, col, size);
}

void dump_results()
{
    FileSystemNode::gpRoot->sortChildren();

    for (int i = 2; i <= 14; i+= 6)
        {
            //int i = 12;
            fprintf (stdout,
                     "\n---\n\n Breakdown at depth %d\n\n"
                     "Total Size    Count\n", i);
            FileSystemNode::gpRoot->dumpAtDepth(i);
        }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
