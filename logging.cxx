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
#include <boost/shared_ptr.hpp>
#include <algorithm>
#include <malloc.h>
#include <assert.h>
#include <string.h>
#include <logging.hxx>

typedef boost::shared_ptr< std::string > SharedString;

static std::set< SharedString > aGlobalNames;

static void globalise_string( SharedString &out, const char *pStr)
{
    SharedString str(new std::string(pStr)); // typical C++ / heinous waste

    std::set< SharedString >::iterator it;
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
};

typedef std::set< AddressRecord > AddressSet;

static AddressSet space;

void
register_compile_unit (const char *name, size_t size)
{
    fprintf (stdout, "compile-unit '%s', size: %ld\n",
             name, (long)size);
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

    AddressRecord aRecord (what->file, what->name,
                           what->line, what->col,
                           start_pc, end_pc);

    AddressSet::const_iterator it = space.find(aRecord);

    if ( it != space.end())
    {
        assert (it->mStart_pc == start_pc);
        if (it->mEnd_pc == end_pc &&
            it->mLine == what->line &&
            it->mCol == what->col)
        {
#if 0 // rather an interesting case here - the source of our grief initially I think.
            if (*it->mFile != what->file)
                fprintf (stderr, "Duplicate inline record start_pcs for two records !"
                         "'%s:%d' (0x%lx->0x%lx) vs '%s:%d' (0x%lx->0x%lx)\n",
                         it->mFile->c_str(), it->mLine, (long)it->mStart_pc, (long)it->mEnd_pc,
                         what->file, what->line, (long)start_pc, (long)end_pc);
#endif
        }
        else
        {
            fprintf (stderr, "Identical start_pcs for two records !"
                     "'%s:%d' (0x%lx->0x%lx) vs '%s:%d' (0x%lx->0x%lx)\n",
                     it->mFile->c_str(), it->mLine, (long)it->mStart_pc, (long)it->mEnd_pc,
                     what->file, what->line, (long)start_pc, (long)end_pc);
            abort();
        }
    }
    else
    {
        space.insert (aRecord);
    }
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
