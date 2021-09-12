#include "rbase.h"
#include <execinfo.h>
#include <unistd.h>  // for isatty()

#ifdef HAVE_LIBBACKTRACE

#include "backtrace.h"

typedef struct BacktraceCtx {
  enum { PASS1, PASS2 } pass;
  int   maxFnNameLen;
  int   limit;
  int   count;
  const char* exepath;
  FILE* fp;
  bool  stylize;
  bool  include_source;
  const char* dimstyle;
  const char* boldstyle;
  const char* nostyle;
  int nsources_printed;
} BacktraceCtx;

static void _bt_error_cb(void* data, const char* msg, int errnum) {
  #ifdef DEBUG
  errlog("[os_stacktrace] error #%d %s", errnum, msg);
  #endif
}

static bool fwrite_sourceline(BacktraceCtx* ctx, FILE* fp, int lineno) {
  bool ok = false;
  char* line = NULL;
  size_t bufsize = 0;
  int linecount = 0;
  int extralines_before = 1;
  int extralines_after = 1;

  while (1) {
    ssize_t len = getline(&line, &bufsize, fp);
    if (len < 0)
      break;
    linecount++;
    if (linecount >= lineno - extralines_before && linecount <= lineno + extralines_after) {
      if (linecount == lineno) {
        ok = true;
        fprintf(ctx->fp, "%s% 6d → | %s", ctx->dimstyle, linecount, ctx->nostyle);
      } else {
        fprintf(ctx->fp, "%s% 6d   | ", ctx->dimstyle, linecount);
      }
      fwrite(line, (size_t)len, 1, ctx->fp);
      fwrite(ctx->nostyle, strlen(ctx->nostyle), 1, ctx->fp);
      if (linecount == lineno + extralines_after)
        break;
    }
  }
  free(line);
  return ok;
}

static int _bt_full_cb(void* data, uintptr_t pc, const char* file, int line, const char* fun) {
  BacktraceCtx* ctx = (BacktraceCtx*)data;
  ctx->count++;
  if (ctx->limit >= 0 && ctx->count > ctx->limit)
    return 0;
  if (ctx->pass == PASS1) {
    if (fun != NULL) {
      int len = (int)strlen(fun);
      if (len > ctx->maxFnNameLen)
        ctx->maxFnNameLen = len;
    }
  } else {
    file = file ? path_cwdrel(file) : NULL;
    FILE* srcfp = NULL;
    if (ctx->include_source && file) {
      if (!(srcfp = fopen(file, "r"))) {
        //dlog("failed to open %s: %s", filename, strerror(errno));
        if (ctx->nsources_printed == 0)
          return 0;
      }
      if (ctx->nsources_printed == 0) {
        fprintf(ctx->fp,
          "%s------------------------------------------------------------------------------%s\n",
          ctx->dimstyle, ctx->nostyle);
      }
    }
    fprintf(ctx->fp, "  %s%-*s%s  %s:%d  %s[pc %p]%s\n",
      ctx->boldstyle, ctx->maxFnNameLen, fun == NULL ? "?" : fun, ctx->nostyle,
      file ? file : "?", line,
      ctx->dimstyle, (void*)pc, ctx->nostyle);
    if (srcfp) {
      fwrite_sourceline(ctx, srcfp, line);
      fclose(srcfp);
      ctx->nsources_printed++;
    } else if (ctx->include_source) {
      fprintf(ctx->fp, "    %s(source unavailable)%s\n", ctx->dimstyle, ctx->nostyle);
    }
  }
  return 0;
}


static void print_limit_reached(const BacktraceCtx* ctx) {
  fprintf(ctx->fp, "  %s+%d frames%s\n",
    ctx->dimstyle, ctx->count - ctx->limit, ctx->nostyle);
}

#endif /* HAVE_LIBBACKTRACE */


void os_stacktrace_fwrite(FILE* nonull fp, int offset_frames, int limit, int limit_src) {
  offset_frames++; // always skip the top frame for this function

  if (limit < 0)
    limit = -1;

  if (limit_src < 0) {
    limit_src = -1;
  } else if (limit_src > limit) {
    limit_src = limit;
  }

#ifdef HAVE_LIBBACKTRACE
  BacktraceCtx ctx = {
    .fp = fp,
    .exepath = os_exepath(),
    .limit = limit,
  };

  if (isatty(fileno(fp))) {
    ctx.stylize = true;
    ctx.dimstyle = "\e[2m";
    ctx.boldstyle = "\e[1m";
    ctx.nostyle  = "\e[0m";
  } else {
    ctx.dimstyle = "";
    ctx.boldstyle = "";
    ctx.nostyle  = "";
  }

  struct backtrace_state* state;
  state = backtrace_create_state(ctx.exepath, 0, _bt_error_cb, &ctx);

  // The following ...works on macos; to provide an invalid program filename.
  // Turns out that the libbacktrace backend for macho doesn't use the filename argument.
  // Or at least AFAIK from skimming the code.
  //state = backtrace_create_state("/proc/self/exe", 0, _bt_error_cb, &ctx);

  // first pass measures max function-name length
  if (backtrace_full(state, offset_frames, _bt_full_cb, _bt_error_cb, &ctx) == 0) {
    // second pass actually prints
    ctx.pass = PASS2;
    ctx.count = 0; // reset counter
    if (backtrace_full(state, offset_frames, _bt_full_cb, _bt_error_cb, &ctx) == 0) {
      // did we hit the limit?
      if (ctx.count > ctx.limit)
        print_limit_reached(&ctx);

      // source lines
      ctx.include_source = true;
      ctx.maxFnNameLen = 0;
      ctx.count = 0; // reset counter
      ctx.limit = limit_src; // may be different limit
      backtrace_full(state, offset_frames, _bt_full_cb, _bt_error_cb, &ctx);
      if (ctx.nsources_printed > 1) {
        if (ctx.count > ctx.limit)
          print_limit_reached(&ctx);
        fputc('\n', ctx.fp);
      }
      return;
    }
  }


  // fall back to libc backtrace()
#endif

  void* buf[128];
  int framecount = backtrace(buf, countof(buf));
  if (framecount < offset_frames)
    return;
  char** strs = backtrace_symbols(buf, framecount);
  if (strs != NULL) {
    if (limit > 0 && limit < framecount)
      framecount = limit;
    for (int i = offset_frames; i < framecount; ++i) {
      fwrite(strs[i], strlen(strs[i]), 1, fp);
      fputc('\n', fp);
    }
    free(strs);
    return;
  }
  // Memory allocation failed;
  // fall back to backtrace_symbols_fd, which doesn't respect offset_frames.
  // backtrace_symbols_fd writes entire backtrace to a file descriptor.
  // Make sure anything buffered for fp is written before we write to its fd
  fflush(fp);
  backtrace_symbols_fd(buf, framecount, fileno(fp));
}
