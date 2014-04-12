/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <stddef.h>

// Not a beautiful API
extern void register_compile_unit (const char *name, size_t size);
extern void register_file_span    (const char *name, int line, int col,
				   size_t size);
extern void dump_results();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
