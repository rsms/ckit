/*
rbase: a utility C library

Copyright (c) 2021, Rasmus Andersson <rsms.me>

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
#ifndef _RBASE_H_
#define _RBASE_H_
#include "defs.h"
#include "atomic.h"

#ifdef __cplusplus
extern "C" {
#endif

// Interfaces in the top-level rbase directory are included here.
// Interfaces of subdirectories need to be explicitly included by
// users of rbase. This header (rbase.h) is usually precompiled.

#include "panic.h"
#include "debug.h"
#include "mem.h"
#include "thread.h"
#include "chan.h"
#include "hash.h"
#include "str.h"
#include "os.h"
#include "unicode.h"
#include "parseint.h"
#include "testing.h"
#include "util.h"
#include "time.h"
#include "path.h"
#include "fs.h"

#ifdef __cplusplus
}
#endif
#endif /* _RBASE_H_ */
