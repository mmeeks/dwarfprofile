/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * a De-Dwarfe'd C++ microcosm:
 */

#include <memory>
#include <vector>
#include <set>
#include <functional>
#include <string>
#include <boost/shared_ptr.hpp>
#include <boost/unordered_set.hpp>
#include <boost/functional/hash.hpp>
#include <algorithm>
#include <malloc.h>
#include <assert.h>
#include <string.h>
#include <logging.hxx>

typedef boost::shared_ptr< std::string > SharedString;

class SharedStringHashEqual {
  public:
    size_t operator ()(const SharedString &str) const
    {
        return boost::hash< std::string >()(*str);
    }
    bool operator ()(const SharedString &a, const SharedString &b) const
    {
        return *a == *b;
    }
};
typedef boost::unordered_set< SharedString, SharedStringHashEqual,
                              SharedStringHashEqual> StringHash;
static StringHash aGlobalNames;

static void globalise_string( SharedString &out, const char *pStr)
{
    SharedString str(new std::string(pStr)); // typical C++ / heinous waste

    StringHash::const_iterator it;
    it = aGlobalNames.find (str);
    if (it == aGlobalNames.end())
    {
        aGlobalNames.insert (str);
        out = str;
    }
    else
        out = *it;
}

struct AddressRecord {
    SharedString mFile, mFunc;
    int mLine, mCol;
    Dwarf_Addr mStart_pc;
    Dwarf_Addr mEnd_pc;

    AddressRecord()
    {
    }
    AddressRecord( const char *file, const char *func,
                   int line, int col,
                   Dwarf_Addr start_pc, Dwarf_Addr end_pc ) :
        mLine (line), mCol (col), mStart_pc (start_pc), mEnd_pc (end_pc)
    {
        globalise_string (mFile, file);
        globalise_string (mFunc, func);
    }

    bool operator<(const AddressRecord &cmp) const
    {
        return mStart_pc < cmp.mStart_pc;
    }

    bool operator==(const AddressRecord &cmp) const
    {
        return mStart_pc == cmp.mStart_pc;
    }

    long pcDifference()
    { // often not a true size
        return mEnd_pc - mStart_pc;
    }
};

typedef std::set< AddressRecord > AddressSet;

static AddressSet space;

void
register_compile_unit (const char *name, size_t size)
{
    fprintf (stdout, "compile-unit '%s', size: %ld\n",
             name, (long)size);
}

void recursive_splitting_insert (const AddressRecord &ins)
{
    AddressSet::const_iterator it = space.find(ins);

    if ( it != space.end())
    {
        assert (it->mStart_pc == ins.mStart_pc);
        if (it->mEnd_pc == ins.mEnd_pc &&
            it->mLine == ins.mLine &&
            it->mCol == ins.mCol)
        {
#if 0 // rather an interesting case here - the source of our grief initially I think.
            if (*it->mFile != what->file)
                fprintf (stderr, "Duplicate inline record start_pcs for two records !"
                         "'%s:%d' (0x%lx->0x%lx) vs '%s:%d' (0x%lx->0x%lx)\n",
                         it->mFile->c_str(), it->mLine, (long)it->mStart_pc, (long)it->mEnd_pc,
                         ins.mFile->c_str(), ins.mLine, (long)ins.mStart_pc, (long)ins.mEnd_pc);
#endif
        }
        else if (it->mEnd_pc == ins.mEnd_pc)
        {
            // ignore - describes the same range : take pot luck
        }
        else
        {
#if 0 // happens really a lot as we sub-divide large die's recursively ...
            fprintf (stderr, "Splitting identical start_pcs for two records !"
                     "'%s:%d' (0x%lx->0x%lx) vs '%s:%d' (0x%lx->0x%lx)\n",
                     it->mFile->c_str(), it->mLine, (long)it->mStart_pc, (long)it->mEnd_pc,
                     ins.mFile->c_str(), ins.mLine, (long)ins.mStart_pc, (long)ins.mEnd_pc);
#endif

            // Nasty case ... lets let the smaller guy win for his
            // range: that makes some sense at least.
            AddressRecord aSmall;
            AddressRecord aLarge;

            if (ins.mEnd_pc < it->mEnd_pc)
            {
                aSmall = ins;
                aLarge = *it;
            }
            else
            {
                aLarge = ins;
                aSmall = *it;
            }
            space.erase(it);

            // chop ...
            aLarge.mStart_pc = aSmall.mEnd_pc + 1;
//            fprintf (stderr,"  split into two pieces: sizes 0x%lx and 0x%lx\n",
//                     aSmall.pcDifference(), aLarge.pcDifference());
            space.insert (aSmall);
            if (aLarge.mEnd_pc > aLarge.mStart_pc)
                recursive_splitting_insert (aLarge);
        }
    }
    else // 95% common case
    {
        space.insert (ins);
    }
}

/*
 * Build a layered series of spans
 */
void register_address_span (struct what_info *what,
                            Dwarf_Addr start_pc, Dwarf_Addr end_pc)
{
//    fprintf (stderr, "start pc 0x%lx end pc 0x%lx\n", start_pc, end_pc);
    if (!what || !what->file)
    {
//        fprintf (stderr, "what!?\n");
        return;
    }

    static int nProgress = 0;
    if ((++nProgress % 4096) == 0)
        fprintf (stderr, ".");

    recursive_splitting_insert (
        AddressRecord (what->file, what->name,
                       what->line, what->col,
                       start_pc, end_pc));
}

void scan_addresses_to_fs_tree()
{
    fprintf (stderr, "* scan address space ...\n");

    AddressSet::const_iterator it = space.begin();
    AddressSet::const_iterator prev = space.begin();
    AddressSet::const_iterator end = space.end();

    if (it != end)
        ++it;

    for (;it != end; ++it)
    {
//        if (prev->mEnd_pc > it->mStart_pc)
//            fprintf (stderr, "overlapping dies\n"); // these happen.

        assert (prev->mStart_pc < it->mStart_pc); // check sorted.

        size_t size = prev->mEnd_pc - prev->mStart_pc;
        assert (size >= 0);

        if (prev->mEnd_pc > it->mStart_pc)
            size = it->mStart_pc - prev->mStart_pc;
        else if (prev->mEnd_pc < it->mStart_pc)
        {
            size_t gap = it->mStart_pc - prev->mEnd_pc;
            if (gap > 4)
                fprintf (stderr, "unusual large gap between "
                         "%s(%s) and %s(%s) 0x%lx -> 0x%lx (%ld bytes)\n",
                         prev->mFile->c_str(), prev->mFunc->c_str(),
                         it->mFile->c_str(), it->mFunc->c_str(),
                         (long)prev->mEnd_pc, (long)prev->mStart_pc,
                         (long)gap);
            fs_register_size ("/gaps", "gap", 0, 0, gap);
        }

        if (size > 0)
            fs_register_size (prev->mFile->c_str(), prev->mFunc->c_str(),
                              prev->mLine, prev->mCol, size);

        prev = it;
    }
    // loose the last element guy, but hey ...
    fprintf (stderr, "check: total size from dies %ld\n",
             (long)(prev->mEnd_pc - space.begin()->mStart_pc));
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
