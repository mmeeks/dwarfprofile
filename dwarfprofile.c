/* gcc -Wall -g -O2 -ldw -o dwarfprofile dwarfprofile.c
 *
 * dwarfprofile.c - produce a tree of size information from a set of
 * dwarf data for a binary.
 *
 *
 * Copyright (C) 2013, Mark J. Wielaard  <mark@klomp.org>
 *
 * This file is free software.  You can redistribute it and/or modify
 * it under the terms of the GNU General Public License (GPL); either
 * version 3, or (at your option) any later version.
 */

#define _GNU_SOURCE

#include <argp.h>
#include <error.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <dwarf.h>
#include <elfutils/libdw.h>
#include <elfutils/libdwfl.h>

// Older versions of elfutils/libdw dwarf.h don't define this one.
#ifndef DW_TAG_GNU_call_site
#define DW_TAG_GNU_call_site 0x4109
#endif

// Are we generating a Flat Calltree Profile Format
static bool generate_fcpf = false;

// Are we generating a Calltree Profile Format
static bool generate_cpf = false;

// Are we generating a XML
static bool generate_xml = false;

// Ignore DIEs without a name (e.g. lexical blocks)
static bool ignore_no_name = false;

// Size used for single address DIEs
static int single_address_size = 1;

// For debugging in flat output show DIE offsets.
static bool show_die_offset = false;

// File strings cache for the current CU.
static Dwarf_Files *files;

static struct argp argp;

static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
    case ARGP_KEY_INIT:
      /* dwfl_standard_argp needs a Dwfl pointer to fill in. */
      state->child_inputs[0] = state->input;
      break;
    case 'f':
      generate_fcpf = true;
      ignore_no_name = true;
      single_address_size = 0;
      break;
    case 'c':
      generate_cpf = true;
      ignore_no_name = true;
      single_address_size = 0;
      break;
    case 'x':
      generate_xml = true;
      break;
    case 'i':
      ignore_no_name = true;
      break;
    case 's':
      single_address_size = atoi (arg);
      break;
    case 'd':
      show_die_offset = true;
      break;
    case ARGP_KEY_FINI:
      if (generate_cpf + generate_xml + generate_fcpf > 1)
	{
	  argp_failure (state, EXIT_FAILURE, 0,
			"Can only generate one format"
			" (XML, CTF or FCTF) at a time.\n");
	  return EINVAL;
	}
    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

/* Returns size of code described by this DIE. Returns zero if this
   DIE doesn't cover any code. 1 is returned for DIEs that do describe
   code by have unknown size. */
static Dwarf_Word
DIE_code_size (Dwarf_Die *die)
{
  Dwarf_Addr base;
  Dwarf_Addr begin;
  Dwarf_Addr end;
  ptrdiff_t off = 0;
  Dwarf_Word size = 0;

  do
    {
      // Also handles lowpc plus highpc as special one range case.
      off = dwarf_ranges (die, off, &base, &begin, &end);
      if (off > 0)
	{
	  size += (end - begin);
	}
    }
  while (off > 0);

  if (size == 0 && (dwarf_hasattr (die, DW_AT_entry_pc)
                    || dwarf_hasattr (die, DW_AT_low_pc)))
    size = single_address_size;

  return size;
}

/* Returns the tag of the DIE declaring the given DIE following
   DW_AT_abstract_origin and DW_AT_specification. */
static int
DIE_decl_tag (Dwarf_Die *die, Dwarf_Die **decl)
{
  Dwarf_Die die_mem;
  Dwarf_Attribute attr_mem;
  Dwarf_Attribute *attr;

  *decl = die;
  do
    {
      attr = dwarf_attr (die, DW_AT_abstract_origin, &attr_mem);
      if (attr == NULL)
	attr = dwarf_attr (die, DW_AT_specification, &attr_mem);
      if (attr == NULL)
	break;

      die = *decl;
      *decl = dwarf_formref_die (attr, &die_mem);
    }
  while (*decl != NULL); /* Wouldn't that actually be an error? */

  return dwarf_tag (die);
}

/* Returns a static constant string representation of the DIE tag.
   Returns NULL when unknown. Would be nice if libdw had this. */
static const char *
TAG_name (int tag)
{
  /* Just recognize code/function DIEs. Add more if necessary. */
  switch (tag)
    {
    case DW_TAG_compile_unit:
      return "compile_unit";
    case DW_TAG_subprogram:
      return "subprogram";
    case DW_TAG_catch_block:
      return "catch_block";
    case DW_TAG_inlined_subroutine:
      return "inlined_subroutine";
    case DW_TAG_lexical_block:
      return "lexical_block";
    case DW_TAG_module:
      return "module";
    case DW_TAG_partial_unit:
      return "partial_unit";
    case DW_TAG_try_block:
      return "try_block";
    case DW_TAG_with_stmt:
      return "with_stmt";
    case DW_TAG_GNU_call_site:
      return "call_site";
    case DW_TAG_label:
      return "label";
    default:
      return NULL;
    }
}

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
  const char *file;
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
  const char *file;
  int line;
  int col;
  Dwarf_Word size;
};

/* Returns the code size of the DIE and fills in the what and where
   info if the size is greater than zero. */
Dwarf_Word
DIE_what_where_size (Dwarf_Die *die,
		     struct what_info *what, struct where_info *where)
{
  Dwarf_Word size = DIE_code_size (die);
  if (size > 0)
    {
      Dwarf_Die *decl;
      what->tag = DIE_decl_tag (die, &decl);
      what->die_off = dwarf_dieoffset (decl);
      what->name = dwarf_diename (die);
      what->file = dwarf_decl_file (die);

      what->line = 0;
      what->col = 0;
      dwarf_decl_line (die, &what->line);
      dwarf_decl_column (die, &what->col);

      if (decl == die)
	{
	  where->tag = what->tag;
	  where->die_off = what->die_off;
	  where->file = what->file;
	  where->line = what->line;
	  where->col = what->col;
	}
      else
	{
	  where->tag = dwarf_tag (die);
	  where->die_off = dwarf_dieoffset (die);

	  Dwarf_Word value;
	  Dwarf_Attribute attr_mem;
	  where->file = what->file;
	  if (dwarf_formudata (dwarf_attr (die, DW_AT_call_file, &attr_mem),
			       &value) == 0)
	    where->file = dwarf_filesrc (files, value, NULL, NULL);

	  where->line = what->line;
	  if (dwarf_formudata (dwarf_attr (die, DW_AT_call_line, &attr_mem),
			       &value) == 0)
	    where->line = value;

	  where->col = what->col;
	  if (dwarf_formudata (dwarf_attr (die, DW_AT_call_column, &attr_mem),
			       &value) == 0)
	    where->col = value;

	  /* XXX is this really right or just cosmetics?  If all
	     information of what and where are the same just pretend
	     what == where anyway. Note we force the what die_off
	     because all information can apparently be derived from
	     the where. */
	  if (where->tag == what->tag
	      && where->file == what->file
	      && where->line == what->line
	      && where->col == what->col)
	    what->die_off = where->die_off;

	}
      where->size = size;
    }
  return size;
}

/* Returns a hopefully unique identifier for what code is being used
   based on the definition tag, name, file, line and col if
   known. String has to be freed by caller. */
static char *
what_identifier_string (const struct what_info *what)
{
  char *res;
  int tag = what->tag;
  const char *name = what->name;
  const char *file = what->file;
  int line = what->line;
  int col = what->col;
  Dwarf_Word die_off = what->die_off;

  if (name != NULL)
    {
      if (file != NULL)
	{
	  if (line != 0)
	    {
	      if (col != 0)
		{
		  if (asprintf (&res, "%s:%s:%s:%d:%d", TAG_name (tag),
				name, file, line, col) < 0)
		    res = NULL;
		}
	      else
		{
		  if (asprintf (&res, "%s:%s:%s:%d", TAG_name (tag),
				name, file, line) < 0)
		    res = NULL;
		}
	    }
	  else
	    {
	      if (asprintf (&res, "%s:%s:%s", TAG_name (tag), name, file) < 0)
		res = NULL;
	    }
	}
      else
	{
	  if (asprintf (&res, "%s:%s", TAG_name (tag), name) < 0)
	    res = NULL;
	}
    }
  else
    {
      // No name, use DIE offset to generate something (possibly non-unique).
      if (asprintf (&res, "%s_%#lx", TAG_name (tag), die_off) < 0)
	res = NULL;
    }

  return res;
}

/* Returns a string describing the location where a DIE was used.
   String has to be freed by caller. */
static char *
where_string (const struct where_info *where)
{
  char *res;
  int tag = where->tag;
  const char *file = where->file;
  int line = where->line;
  int col = where->col;

  if (file != NULL)
    {
      if (line != 0)
	{
	  if (col != 0)
	    {
	      if (asprintf (&res, "%s:%s:%d:%d", TAG_name (tag),
			    file, line, col) < 0)
		res = NULL;
	    }
	  else
	    {
	      if (asprintf (&res, "%s:%s:%d", TAG_name (tag),
			    file, line) < 0)
		res = NULL;
	    }
	}
      else
	{
	  if (asprintf (&res, "%s:%s", TAG_name (tag), file) < 0)
	    res = NULL;
	}
    }
  else
    {
      if (asprintf (&res, "%s", TAG_name (tag)) < 0)
	res = NULL;
    }

  return res;
}

/* We treat nested subprograms as "inlines", keep track of how deep we nest. */
static int in_top_level_subprogram = 0;

static void
output_die_begin (struct what_info *what, struct where_info *where, int indent)
{
  if (generate_fcpf)
    {
      // All is done in output_die_end.
    }
  else if (generate_cpf)
    {
      /* We report "top-level" functions (subprograms), but don't yet
	 report any actual bytes here. First we will report all
	 inlines (children) as calls in output_die_end. Then, also in
	 output_die_end, we will report the actual bytes for the
	 function (minus the children/inlined sizes). */
      if (where->tag == DW_TAG_subprogram)
	{
	  /* Nested functions... bleah... */
	  in_top_level_subprogram++;
	  if (in_top_level_subprogram == 1)
	    {
	      if (what->file == NULL)
		fprintf (stderr, "WARNING: [%" PRIx64 "] %s"
			 " has no file.\n",
			 what->die_off, what_identifier_string (what));
	      else
		printf ("fl=%s\n", what->file);
	      printf ("fn=%s\n", what->name);
	    }
	}
    }
  else
    {
      printf ("%*s", indent, "");
      char *what_id = what_identifier_string (what);
      if (generate_xml)
	{
	  // XXX crappy fake XML...
	  printf ("<die");
	  if (show_die_offset)
	    printf (" off='%lx'", where->die_off);
	  printf (" id='%s'\n", what_id);
	  printf ("%*s    ", indent, "");
	  printf (" what_tag='%s' what_file='%s' what_line='%d'"
		  " what_col='%d'\n", TAG_name (what->tag),
		  (what->file ?: ""), what->line, what->col);
	  printf ("%*s    ", indent, "");
	  printf (" where_tag='%s' where_file='%s' where_line='%d'"
		  " where_col='%d'>\n", TAG_name (where->tag),
		  (where->file ?: ""), where->line, where->col);
	}
      else
	{
	  if (show_die_offset)
	    printf ("[%" PRIx64 "] ", where->die_off);

	  if (what->die_off == where->die_off)
	    printf ("%s (%ld)\n", what_id, where->size);
	  else
	    {
	      char *where_str = where_string (where);
	      printf ("%s@%s (%ld)\n", what_id, where_str, where->size);
	      free (where_str);
	    }
	}
      free (what_id);
    }
}

static void
output_die_end (struct what_info *what, struct where_info *where,
		Dwarf_Word children_size, int indent)
{
  if (generate_cpf || generate_fcpf)
    {
      /* Only report on "real" code. */
      if (what->tag == DW_TAG_compile_unit)
	return;

      if (generate_fcpf)
	{
	  /* Report the size of what was included minus the size
	     of any children reported to prevent double counting. */
	  if (what->file == NULL)
	    fprintf (stderr, "WARNING: [%" PRIx64 "] %s"
		     " has no file.\n",
		     what->die_off, what_identifier_string (what));
	  else
	    printf ("fl=%s\n", what->file);
	  printf ("fn=%s\n", what->name);
	  if (what->line == 0)
	    fprintf (stderr, "WARNING: [%" PRIx64 "] %s"
		     " has no line info.\n",
		     what->die_off, what_identifier_string (what));
	  printf ("%d %ld\n\n", what->line, where->size - children_size);
	}
      else /* generate_cpf */
	{
	  if (where->tag != DW_TAG_subprogram
	      && in_top_level_subprogram == 0)
	    {
	      fprintf (stderr,
		       "WARNING: Cannot happen! Embedded code outside"
		       " a subprogram %s\n",
		       what_identifier_string (what));
	      return;
	    }

	  if (where->tag == DW_TAG_subprogram)
	    {
	      in_top_level_subprogram--;
	      if (in_top_level_subprogram < 0)
		{
		  fprintf (stderr,
			   "WARNING: Cannot happen! Unbalanced subprogram %s\n",
			   what_identifier_string (what));
		  return;
		}

	      /* All other subprograms are treated like line inlined
		 subroutines below. */
	      if (in_top_level_subprogram == 0)
		{
		  if (children_size == 0)
		    {
		      /* No embedded code/calls reported since begin. */
		      printf ("%d %ld\n\n", what->line, where->size);
		    }
		  else
		    {
		      /* Just report the file/function name again to avoid
			 "confusion". */
		      printf ("\nfl=%s\n", what->file);
		      printf ("fn=%s\n", what->name);
		      printf ("%d %ld\n\n", what->line,
			      where->size - children_size);
		    }
		}
	    }

	  if (where->tag != DW_TAG_subprogram
	      || in_top_level_subprogram > 0)
	    {
	      printf ("cfl=%s\n", what->file);
	      printf ("cfn=%s\n", what->name);
	      printf ("calls=1 %d\n", what->line);
	      printf ("%d %ld\n", where->line, where->size - children_size);
	    }
	}
    }
  else if (generate_xml)
    {
      printf ("%*s<size children='%ld' total='%ld'>\n", indent + 1, "",
	      children_size, where->size);
      printf ("%*s</die>\n", indent, "");
    }
  else
    {
      printf ("%*send %s (%ld,%ld)\n", indent, "",
	      what_identifier_string (what), children_size, where->size);
    }
}

/* Walks all (code) children of the given DIE and returns the total
   code size. */
static Dwarf_Word
walk_children (Dwarf_Die *die, int indent)
{
  Dwarf_Word total = 0;
  if (! dwarf_haschildren (die))
    return total;

  Dwarf_Die child;
  if (dwarf_child (die, &child) == 0)
    {
      do
	{
	  struct what_info what;
	  struct where_info where;

	  /* Only DIEs with a code size have children with code and
	     the code size of a DIE >= the sum of the code size of the
	     children. */
	  Dwarf_Word size = DIE_what_where_size (&child, &what, &where);
	  if (size > 0)
	    {
	      /* Even if we don't use this DIE because it doesn't have
		 a name, we still want to walk the children. */
	      bool use_die = ((what.name != NULL) || (! ignore_no_name));

	      if (use_die)
		{
		  /* Note we add the whole DIE size, which include the
		     size of all children. So only add children_size
		     below if we don't report this DIE. */
		  total += size;

		  output_die_begin (&what, &where, indent);
		}

	      Dwarf_Word children_size = walk_children (&child, indent + 1);

	      if (use_die)
		output_die_end (&what, &where, children_size, indent);
	      else
		total += children_size;
	    }
	}
      while (dwarf_siblingof (&child, &child) == 0);
    }

  return total;
}

static void
output_cu_begin (struct what_info *what, struct where_info *where)
{
  output_die_begin (what, where, 2); // indent 2 (dwarfprofile + module).
}

static void
output_cu_end (struct what_info *what, struct where_info *where,
	       Dwarf_Word children_size)
{
  output_die_end (what, where, children_size, 2);
}

static void
handle_cu (Dwarf_Die *cu)
{
  /* Skip CUs without any code. */
  Dwarf_Word size = DIE_code_size (cu);
  const char *name = dwarf_diename (cu);

  // XXX ehe, name == NULL, when does that happen?
  if (size == 0 || name == NULL)
    return;

  /* Construct a (short) name and file to refer to this CU. */
  const char *short_name = rindex (name, '/');
  short_name = (short_name != NULL) ? short_name + 1 : name;

  Dwarf_Attribute attr;
  const char *dir = dwarf_formstring (dwarf_attr (cu, DW_AT_comp_dir, &attr));

  char *full_name;
  const char *file = name;
  if (dir != NULL && name[0] != '/')
    {
      if (asprintf (&full_name, "%s/%s", dir, name) != -1)
	file = full_name;
    }

  /* Compile Unit DIEs only really have where info, but construct a
     what for consistency. XXX Need to handle imported_unit/partial_units? */
  struct where_info where;
  struct what_info what;
  where.tag = what.tag = dwarf_tag (cu);
  where.die_off = what.die_off = dwarf_dieoffset (cu);
  what.name = short_name;
  where.file = what.file = file;
  where.line = what.line = 0;
  where.col = what.col = 0;
  where.size = size;

  /* cache the file list for this CU. */
  if (dwarf_getsrcfiles (cu, &files, NULL) != 0)
    files = NULL; // There better not be any DW_AT_desc_files...

  output_cu_begin (&what, &where);
  Dwarf_Word children_size = walk_children (cu, 3); // indent 3 (dp/mod/cu)
  output_cu_end (&what, &where, children_size);

  if (file != name)
    free (full_name);
}

static void
output_module_begin (const char *name)
{
  if (generate_cpf | generate_fcpf)
    {
      // XXX Anything?
    }
  else if (generate_xml)
    {
      printf (" <module name='%s'>\n", name);
    }
  else
    printf (" module %s\n", name);
}

static void
output_module_end (const char *name)
{
  if (generate_cpf | generate_fcpf)
    {
      // XXX Anything?
    }
  else if (generate_xml)
    {
      printf (" <module name='%s'>\n", name);
    }
  else
    printf (" module %s done\n", name);
}

static int
handle_module (Dwfl_Module *mod, void **userdata, const char *name,
	       Dwarf_Addr base, void *arg)
{
  Dwarf_Die *cu = NULL;
  Dwarf_Addr bias;

  output_module_begin (name);
  while ((cu = dwfl_module_nextcu (mod, cu, &bias)) != NULL)
    handle_cu (cu);
  output_module_end (name);

  return DWARF_CB_OK;
}

void
output_start ()
{
  if (generate_cpf || generate_fcpf)
    {
      printf ("version: 1\ncreator: dwarfprofile\n\n");
      printf ("events: Bytes\n\n");
    }
  else if (generate_xml)
    {
      printf ("<dwarfprofile>\n");
    }
  else
    printf ("dwarfprofile\n");
}

void
output_end ()
{
  if (generate_cpf || generate_fcpf)
    {
      // XXX Anything?
    }
  else if (generate_xml)
    {
      printf ("</dwarfprofile>\n");
    }
  else
    printf ("dwarfprofile done\n");
}

int
main (int argc, char **argv)
{
  const struct argp_option options[] =
    {
      { NULL, 0, NULL,  0, "Output selection options:", 2 },
      { "flatcalltree", 'f', NULL, 0,
	"Output Flat Calltree Profile Format (implies -i -s0)", 0 },
      { "calltree", 'c', NULL, 0,
	"Output Calltree Profile Format (implies -i -s0)", 0 },
      { "xml", 'x', NULL, 0, "XML output", 0 },

      { NULL, 0, NULL,  0, "Code DIE selection options:", 3 },
      { "ignore-no-name", 'i', NULL, 0,
	"Ignore code DIEs without a name (e.g. lexical_blocks)", 0 },
      { "single-address", 's', "size", 0,
	"Size to use for single-address DIEs (e.g. labels or call_sites,"
	" which only have a DW_AT_low_pc, but not DW_AT_high_pc)."
	" Defaults to 1. When 0, single-address DIEs are ignored", 0 },

      { NULL, 0, NULL, 0, ("Miscellaneous:"), 0 },
      // Anything else (help, usage, etc.)
      { "die-offsets", 'd', NULL, 0, "Show DIE offsets (debug only)", 0 },

      { NULL, 0, NULL, 0, NULL, 0 }
    };

  const struct argp_child argp_children[] =
    {
      {	.argp = dwfl_standard_argp () },
      {	.argp = NULL },
    };

  argp.children = argp_children;
  argp.options = options;
  argp.parser = parse_opt;

  int cnt;
  Dwfl *dwfl = NULL;
  error_t e = argp_parse (&argp, argc, argv, 0, &cnt, &dwfl);

  if (e != 0 || dwfl == NULL)
    exit (-1);

  output_start ();
  ptrdiff_t res = dwfl_getmodules (dwfl, handle_module, NULL, 0);
  if (res != 0) // We should handle all modules, anything else is an error
    {
      fprintf (stderr, "dwfl_getmodules failed: %s\n",  dwfl_errmsg (-1));
      exit (-1);
    }
  output_end ();

  dwfl_end (dwfl);

  return 0;
}
