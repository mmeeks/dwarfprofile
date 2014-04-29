/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <dwarf.h>
#include <elfutils/libdw.h>
#include <elfutils/libdwfl.h>
#include <stddef.h>

/* Note that DIE offsets are only unique for a specific Dwfl module or
   file. We do keep them around for debugging (or to generate a name
   for unknown code blobs), but a code definition cannot be identified
   by just a DIE offset. And a code definition can be duplicated in
   different Dwfl modules or files, so we like to identify them by
   name/file/line/col whenever possible. */

/* What code is being used? The tag will always be set. The name,
   file, line and col can be unknown. This (file, line, col if known)
   refer to the definition of the code location, not where or how much
   of the code is used, see where_info. The die_off is only used for
   debugging or when the name is unknown. */
struct what_info
{
  int tag;
  Dwarf_Off die_off;
  const char *name;
  char *file;
  int line;
  int col;
};

/* Where (and how) was the code used? The tag, file, line and col can
   be identical to the what_info if the definition and use are in the
   same spot. The tag will always be set, the file, line and col might
   be missing. The size will always be non-zero and indicates how much
   code is being used in this position (even if code uses the same
   what_info it can use different amounts of the code). The die_off is
   used for debugging (-d) and to see whether what == where.*/
struct where_info
{
  int tag;
  Dwarf_Off die_off;
  char *file;
  int line;
  int col;
  Dwarf_Word size;
};

// Not a beautiful API
extern void register_compile_unit (const char *name, size_t size);

// build map of which address does what
extern void register_address_span (struct what_info *what,
                                   Dwarf_Addr start_pc, Dwarf_Addr end_pc);
extern void scan_addresses_to_fs_tree ();

// when parsing map - map it to file system free
extern void fs_register_size (const char *path, const char *func,
                              int line, int col, size_t size);

extern void dump_results ();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
