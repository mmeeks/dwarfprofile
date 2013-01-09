/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

// dwarffuncs.c - Get some size statistics about (inlined) functions.
// gcc -Wall -g -O2 -ldw -o dwarffuncs dwarffuncs.c
//
// Copyright (C) 2013, Mark J. Wielaard  <mark@klomp.org>
//
// This file is free software.  You can redistribute it and/or modify
// it under the terms of the GNU General Public License (GPL); either
// version 3, or (at your option) any later version.

#include <argp.h>
#include <inttypes.h>
#include <error.h>
#include <stdlib.h>
#include <stdio.h>

#include <dwarf.h>
#include <elfutils/libdw.h>
#include <elfutils/libdwfl.h>

// For the current CU.
static Dwarf_Files *files;

static ptrdiff_t
func_size (Dwarf_Die *func)
{
  Dwarf_Addr base;
  Dwarf_Addr begin;
  Dwarf_Addr end;
  ptrdiff_t off = 0;
  ptrdiff_t size = 0;

  do
    {
      // Also handles lowpc plus highpc as special one range case.
      off = dwarf_ranges (func, off, &base, &begin, &end);
      if (off > 0)
	size += (end - begin);
    }
  while (off > 0);

  return size;
}

static void
print_func (Dwarf_Die *func)
{
  const char *name = dwarf_diename (func);
  const char *file = dwarf_decl_file (func);
  int line = -1;
  dwarf_decl_line (func, &line);
  Dwarf_Off dieoff = dwarf_dieoffset (func);
  ptrdiff_t size = func_size (func);

  printf ("[%" PRIx64 "] %s:%s:%d (%td)", dieoff, name, file, line, size);
}

static void
print_inlined (Dwarf_Die *instance)
{
  const char *name = dwarf_diename (instance);
  Dwarf_Attribute attr_mem;
  Dwarf_Word value;
  const char *file = "<unknown>";
  int line = -1;
  Dwarf_Off dieoff = dwarf_dieoffset (instance);
  ptrdiff_t size = func_size (instance);

  if (dwarf_formudata (dwarf_attr (instance, DW_AT_call_file, &attr_mem),
		       &value) == 0)
    file = dwarf_filesrc (files, value, NULL, NULL);

  if (dwarf_formudata (dwarf_attr (instance, DW_AT_call_line, &attr_mem),
		       &value) == 0)
    line = value;

  printf ("[%" PRIx64 "] %s:%s:%d (%td)", dieoff, name, file, line, size);
}

static int
handle_instance (Dwarf_Die *instance, void *arg)
{
  ptrdiff_t *total = (ptrdiff_t *) arg;
  *total += func_size (instance);

  printf ("  instance ");
  print_inlined (instance);
  printf ("\n");

  return DWARF_CB_OK;
}

static void
handle_inline (Dwarf_Die *func)
{
  printf ("inline ");
  print_func (func);
  printf ("\n");

  ptrdiff_t total = 0;
  if (dwarf_func_inline_instances (func, handle_instance, &total) != 0)
    error (-1, 0, "dwarf_func_inline_instances: %s\n", dwarf_errmsg (-1));

  printf ("total inline ");
  print_func (func);
  printf (": %td\n", total);
}

static void walk_func (Dwarf_Die *func, int indent);

static void
handle_function (Dwarf_Die *func, int indent)
{
  printf ("%*sfunction ", indent, "");
  print_func (func);
  printf ("\n");

  walk_func (func, indent);
}

static void
walk_func (Dwarf_Die *func, int indent)
{
  if (! dwarf_haschildren (func))
    return;

  Dwarf_Die child;
  dwarf_child (func, &child);
  do
    {
      int tag = dwarf_tag (&child);
      switch (tag)
	{
	case DW_TAG_subprogram:
	  {
	    // recurses itself.
	    handle_function (&child, indent + 1);
	    break;
	  }

	case DW_TAG_inlined_subroutine:
	  {
	    printf ("%*sinlined ", indent + 1, "");
	    print_func (&child);
	    printf ("\n");
	    walk_func (&child, indent + 1);
	    break;
	  }

	default:
	  {
	    // Something unrecognized that still has a code size?
	    ptrdiff_t size = func_size (&child);
	    if (size > 0)
	      {
		printf ("%*sunknown_%x ", indent + 1, "", tag);
		print_func (&child);
		printf ("\n");
	      }
	    walk_func (&child, indent + 1);
	  }

	  // XXX DW_TAG_imported_unit hohum....
	}
    }
  while (dwarf_siblingof (&child, &child) == 0);
}

static int
handle_func (Dwarf_Die *func, void *arg)
{
  int abstract_inline = dwarf_func_inline (func);
  Dwarf_Addr lowpc = 0;
  dwarf_lowpc (func, &lowpc);

  if (abstract_inline)
    handle_inline (func);
  else if (lowpc != 0)
    handle_function (func, 0);
  else
    {
      printf ("declaration ");
      print_func (func);
      printf ("\n");
    }

  return DWARF_CB_OK;
}

static void
handle_cu (Dwarf_Die *cu)
{
  const char *name = dwarf_diename (cu);
  Dwarf_Off dieoff = dwarf_dieoffset (cu);
  Dwarf_Attribute attr;
  const char *dir = dwarf_formstring (dwarf_attr (cu, DW_AT_comp_dir, &attr));

  printf ("CU [%" PRIx64 "] %s/%s\n", dieoff, dir, name);

  if (dwarf_getsrcfiles (cu, &files, NULL) != 0)
    error (-1, 0, "dwarf_getsrcfiles: %s\n", dwarf_errmsg (-1));

  ptrdiff_t off = 0;
  do
    off = dwarf_getfuncs (cu, handle_func, NULL, off);
  while (off > 0);

  if (off < 0)
    error (-1, off, "dwarf_getfuncs: %s\n", dwarf_errmsg (-1));
}

int
main (int argc, char **argv)
{
  int cnt;
  Dwfl *dwfl = NULL;
  error_t e = argp_parse (dwfl_standard_argp (), argc, argv, 0, &cnt, &dwfl);

  if (e != 0 || dwfl == NULL)
    exit (-1);

  Dwarf_Die *cu = NULL;
  Dwarf_Addr bias;
  while ((cu = dwfl_nextcu (dwfl, cu, &bias)) != NULL)
    handle_cu (cu);

  return 0;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
