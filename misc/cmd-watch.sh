
_help() {
  cat <<- _EOF_ >&2
Watch files and rebuild as they change.
Usage: $PROG $COMMAND [options] [--] [build args]
options:
  -r, -run    Run target in build directory after successful build.
  -r=<arg>    Run <arg> after successful build.
  -rsh=<cmd>  Run <cmd> in a shell after successful build.
  -wf=<file>  Extra file or directory to watch for changes.
$(_help_common_options)
See \`$PROG build -help\` for rest of arguments accepted.

-r=<arg>, -run=<arg>, -rsh=<cmd>
  Run <arg> after successful build. -rsh runs <cmd> in shell.
  -r=<arg> Can be specified multiple times for additional args to cmd.
  However only the last -rsh=<cmd> is used and replaces any previous -r=<arg>
  or -rsh=<cmd> arguments.
  The string "{BUILD}" is replaced with the absolute path to the effective
  build directory. {BUILD} is included in PATH when running the command.
  If both -r and -r=<arg> is specified, -r is ignored.

Examples:
  $PROG watch test
    Build & run tests as their source files change.
    This works without -r since the default "test" target runs tests itself.

  $PROG watch -r -release myprog
    Build & run myprog in release mode.

  $PROG watch -r=myprog -r=somearg
    Build all targets, then run the command myprog with argument somearg.

  $PROG watch foo -rsh='myprog "\$(date)"'
    Build target foo and run the shell script \`myprog "\$(date)"\`
_EOF_
}

# -----------------------------------------------------------------------------------
# begin watch program

RUN_ARGS=()
BUILD_ARGS=()
BUILD_TARGET=
BUILD_DIR=
BUILD_CMD=build
OPT_BUILD_TYPE=debug  # "debug" | "fast" | "safe"
OPT_TEST=
OPT_RUN=false
OPT_RUN_SHELL=false
EXTRA_WATCH_FILES=()

while [[ $# -gt 0 ]]; do case "$1" in
  -r|-run|--run) OPT_RUN=true; shift ;;
  -r=*)      RUN_ARGS+=( "${1:3}" ); shift ;;
  -run=*)    RUN_ARGS+=( "${1:5}" ); shift ;;
  -rsh=*)    RUN_ARGS=( "${1:5}" );  OPT_RUN_SHELL=true; shift ;;
  -wf=*)     EXTRA_WATCH_FILES+=( "${1:4}" ); shift ;;

  # options for both build and watch
  -fast|-release) BUILD_ARGS+=( "$1" ); OPT_BUILD_TYPE=fast; shift ;;
  -safe)          BUILD_ARGS+=( "$1" ); OPT_BUILD_TYPE=safe; shift ;;
  -debug)         BUILD_ARGS+=( "$1" ); OPT_BUILD_TYPE=debug; shift ;;
  -B)             BUILD_ARGS+=( "$1" ); _expectarg "$1" "$2"; BUILD_DIR="$2"; shift; shift ;;
  -T|-test)       BUILD_ARGS+=( "$1" ); OPT_TEST=y; shift ;;

  --) shift; break ;;
  -*)
    set +e ; _try_parse_common_option "$@" ; N=$? ; set -e
    if [ $N -gt 0 ]; then
      for i in `seq $N`; do
        BUILD_ARGS+=("$1") ; shift
      done
    fi
    ;;
  *) break ;;
esac; done

# Second option parse loop for build command.
# Note: Keep in sync with code in cmd-build.sh
while [[ $# -gt 0 ]]; do case "$1" in
  -*) BUILD_ARGS+=( "$1" ); shift ;;
  *)
    if [ -n "$BUILD_TARGET" ]; then
      BUILD_ARGS+=( "$@" )
      break
    fi
    BUILD_TARGET=$1
    if [ "$BUILD_TARGET" != "test" ]; then
      BUILD_ARGS+=( "$1" )
    fi
    shift
    ;;
esac; done

# check: watch -r target
if $OPT_RUN && [ ${#RUN_ARGS[@]} -eq 0 ]; then
  if [ -z "$BUILD_TARGET" ]; then
    _errhelp "-r without explicit target (name an explicit target or use -r=<arg>)"
  fi
  RUN_ARGS=( "$BUILD_TARGET" )
fi

SRC_DIR=$PWD
CKIT_PKG_DIR="$CKIT_DIR/pkg"

case "$BUILD_TARGET" in
  test)
    OPT_TEST=y
    BUILD_CMD=test  # run "ckit test" instead of "ckit build"
    ;;
  test*)
    OPT_TEST=y
    ;;
esac
if [ -n "$OPT_TEST" ]; then
  BUILD_ARGS+=(-T)
fi

# BUILD_DIR
[ -n "$BUILD_DIR" ] ||
  BUILD_DIR=$PWD/out/$(_build_dir_suffix "$OPT_BUILD_TYPE" $OPT_TEST)
DEPS_FILE=$BUILD_DIR/.deps.txt

# RUN_ARGS: replace "{BUILD}" with the value of $BUILD_DIR
if [ ${#RUN_ARGS[@]} -gt 0 ]; then
  run0=${RUN_ARGS[0]}
  run0=${run0//"{BUILD}"/$BUILD_DIR}
  RUN_ARGS[0]=$run0
  RUN_ARG0_PRINT=$(_nicedir "$run0")
fi

# SHELL needed for OPT_RUN_SHELL
[ -n "$SHELL" ] || SHELL=sh

# LIST_FORMATTER is a program for printing lists. Uses "column" if available.
LIST_FORMATTER=cat
if command -v column >/dev/null; then
  LIST_FORMATTER=column
fi

# FSWATCH_TOOL: Select filesystem watch tool.
# Note: When changing this list, also update _watch_files_for_changes
FSWATCH_TOOL=
FSWATCH_TOOLS_LIST=( fswatch inotifywatch )
for cmd in "${FSWATCH_TOOLS_LIST[@]}"; do
  if command -v $cmd >/dev/null; then
    FSWATCH_TOOL=$cmd
    break
  fi
done
if [ -z "$FSWATCH_TOOL" ]; then
  _err "No filesystem \"watch\" tool is available on this system. Tried: ${FSWATCH_TOOLS_LIST[@]}"
fi

if $VERBOSE; then
  echo "-----------------------------------------------------------"
  echo "CKIT_DIR        $CKIT_DIR"
  echo "CKIT_EXE        $CKIT_EXE"
  echo "SRC_DIR         $SRC_DIR"
  echo "BUILD_CMD       $BUILD_CMD"
  echo "BUILD_ARGS      [${BUILD_ARGS[@]}"]
  echo "BUILD_DIR       $BUILD_DIR"
  echo "BUILD_TARGET    $BUILD_TARGET"
  echo "TEST_ENABLED    $([ -n "$OPT_TEST" ] && echo yes || echo no)"
  echo "RUN_ARGS        [${RUN_ARGS[@]}]"
  echo "FSWATCH_TOOL    $FSWATCH_TOOL"
  echo "LIST_FORMATTER  $LIST_FORMATTER"
  echo "SHELL           $SHELL"
  echo "-----------------------------------------------------------"
fi

SOURCES=()

# WATCH_FILES is a list of SOURCES + files that we watch for changes.
# This list is reset on every rebuild as it may change in case a source file
# includes a new header (or stops including one), or cmakelists.txt changes.
WATCH_FILES=()

WATCH_INDICATOR_PIDFILE="$(_tmpfile).pid"
WATCHER_FILE="$(_tmpfile)"
WATCHER_LOGFILE="$WATCHER_FILE.log"
WATCHER_PIDFILE="$WATCHER_FILE.pid"

RUN_PIDFILE=
if [ ${#RUN_ARGS[@]} -gt 0 ]; then
  RUN_PIDFILE="$(_tmpfile).pid"
  ATEXIT+=( "_pidfile_kill '$RUN_PIDFILE'" )
fi

# _is_source_file <file> -- returns true if <file> is in SOURCES
_is_source_file() {
  for f in "${SOURCES[@]}"; do
    if [ "$f" == "$1" ]; then
      return 0
    fi
  done
  return 1
}

_signal_name() {
  case "$1" in
     1)   echo SIGHUP; return 0 ;;
     2)   echo SIGINT; return 0 ;;
     3)   echo SIGQUIT; return 0 ;;
     4)   echo SIGILL; return 0 ;;
     5)   echo SIGTRAP; return 0 ;;
     6)   echo SIGABRT; return 0 ;;
     7)   echo SIGEMT; return 0 ;;
     8)   echo SIGFPE; return 0 ;;
     9)   echo SIGKILL; return 0 ;;
     10)  echo SIGBUS; return 0 ;;
     11)  echo SIGSEGV; return 0 ;;
     12)  echo SIGSYS; return 0 ;;
     13)  echo SIGPIPE; return 0 ;;
     14)  echo SIGALRM; return 0 ;;
     15)  echo SIGTERM; return 0 ;;
     16)  echo SIGURG; return 0 ;;
     17)  echo SIGSTOP; return 0 ;;
     18)  echo SIGTSTP; return 0 ;;
     19)  echo SIGCONT; return 0 ;;
     20)  echo SIGCHLD; return 0 ;;
     21)  echo SIGTTIN; return 0 ;;
     22)  echo SIGTTOU; return 0 ;;
     23)  echo SIGIO; return 0 ;;
     24)  echo SIGXCPU; return 0 ;;
     25)  echo SIGXFSZ; return 0 ;;
     26)  echo SIGVTALRM; return 0 ;;
     27)  echo SIGPROF; return 0 ;;
     28)  echo SIGWINCH; return 0 ;;
     29)  echo SIGINFO; return 0 ;;
     30)  echo SIGUSR1; return 0 ;;
     31)  echo SIGUSR2; return 0 ;;
   esac
   echo "SIG#$1"
}

_run_after_build() {
  _pidfile_kill "$RUN_PIDFILE"
  set +e
  # pushd "$USER_PWD" >/dev/null
  # local cmd=$(_relpath "$RUN_EXE")
  # echo "$cmd" $RUN_ARGS
  # [[ "$cmd" == "/" ]] || cmd="./$cmd"
  _logv "exec ${RUN_ARGS[@]}"
  (
    if $OPT_RUN_SHELL; then
      PATH="$BUILD_DIR:$PATH" "$SHELL" -c "${RUN_ARGS[@]}" &
      _pid=$!
    else
      PATH="$BUILD_DIR:$PATH" "${RUN_ARGS[@]}" &
      _pid=$!
    fi
    echo $_pid > "$RUN_PIDFILE"
    echo "$RUN_ARG0_PRINT [$_pid] started"
    set +e
    wait $_pid
    _status=$?
    set -e
    if [ $_status -eq 0 ]; then
      printf "$RUN_ARG0_PRINT [$_pid] \e[1;32mexited $_status\e[0m\n"
    else
      if [ $_status -gt 127 ]; then
        # signal
        _status=`expr 128 - $_status`
        _status="$_status ($(_signal_name ${_status:1}))"
      fi
      printf "$RUN_ARG0_PRINT [$_pid] \e[1;31mexited $_status\e[0m\n"
    fi
    rm -rf "$RUN_PIDFILE" ) &
  # popd >/dev/null
  set -e
}

_watch_indicator() {
  local spin=( '...' ' ..' '  .' '   ' '.  ' '.. ' )
  local n=${#spin[@]}
  local i=$(( $n - 1 )) # -1 so we start with spin[0]
  local style_start="\e[2m" # 2 = dim/faint/low-intensity
  local style_end="\e[m"
  if [ ! -t 1 ]; then
    style_start=
    style_end=
  fi
  # wait for run to exit (has no effect if -run is not used)
  while true; do
    [ -f "$RUN_PIDFILE" ] || break
    sleep 0.5
  done
  while true; do
    i=$(( (i+1) % $n ))
    printf "\r${style_start}Waiting for changes to source files${spin[$(( $i ))]}${style_end} "
    sleep 0.5
  done
}

_watch_indicator_stop() {
  _pidfile_kill "$WATCH_INDICATOR_PIDFILE"
  printf "\r"
}

_watch_files_for_changes() {
  case "$FSWATCH_TOOL" in

  fswatch)
    fswatch --one-event --latency=0.2 --extended "${WATCH_FILES[@]}"
    ;;

  inotifywatch) # part of inotifytools
    while true; do
      inotifywatch -P "${WATCH_FILES[@]}" > "$WATCHER_LOGFILE" 2>/dev/null &
      watcher_pid=$!
      echo $watcher_pid > "$WATCHER_PIDFILE"
      sleep 1
      kill -HUP $watcher_pid
      wait $watcher_pid
      if grep -q -v "No events" "$WATCHER_LOGFILE"; then
        break
      fi
    done
    ;;

  *) _err "internal error: unexpected FSWATCH_TOOL='$FSWATCH_TOOL'"
  esac
}

_scan_source_files() {
  # attempt to read source file list generated by ckit.cmake.
  # Each line lists a source file's absolute path.
  local srclist_file=${BUILD_DIR}/${BUILD_TARGET}.ckit-sources.txt
  if [ -n "$OPT_TEST" ]; then
    srclist_file=${BUILD_DIR}/test-${BUILD_TARGET}.ckit-sources.txt
  fi
  _logv "_scan_source_files looking for $srclist_file" >&2
  if [ ! -f "$srclist_file" ]; then
    # guess
    srclist_file=$(_tmpfile)
    case "$BUILD_TARGET" in
      test*)
        find "${BUILD_DIR}" \
          -maxdepth 2 -type f -name 'test-*.ckit-sources.txt' \
          -exec cat '{}' ';' > "$srclist_file"
        ;;
      *)
        # all -- concat all *.ckit-sources.txt files together; sources for all targets
        find "${BUILD_DIR}" \
          -maxdepth 2 -type f -name '*.ckit-sources.txt' \
          -not -name 'test-*' \
          -exec cat '{}' ';' > "$srclist_file"
        ;;
    esac
  fi
  echo "$srclist_file"
}

ATEXIT+=( "_pidfile_kill '$WATCH_INDICATOR_PIDFILE'" )
FIRST_RUN=true
WATCH_FILES_FILE=$(_tmpfile)

while true; do
  # clear screen ("scroll to top" style)
  # However, don't clear first time in verbose mode.
  $FIRST_RUN && $VERBOSE || printf "\x1bc"

  # build
  BUILD_OK=false
  if "$CKIT_EXE" $BUILD_CMD "${BUILD_ARGS[@]}"; then
    BUILD_OK=true
    [ ${#RUN_ARGS[@]} -eq 0 ] || _run_after_build
  # elif $FIRST_RUN; then
  #   # build failed immediately
  #   exit 1
  fi
  FIRST_RUN=false

  # scan for source files (populates SOURCES)
  SOURCES=()
  SOURCE_LIST_FILE=$(_scan_source_files)
  [ -f "$SOURCE_LIST_FILE" ] || _err "No source files found for target $BUILD_TARGET"
  cp -f "$SOURCE_LIST_FILE" "$WATCH_FILES_FILE"

  # scan for source deps
  #
  # Use ninja's "deps" tool which produces a text file of the depencency graph.
  # The deps file is structured like this:
  #   entry      = dependant dependency+ <emptyline>
  #   emptyline  = <LF>
  #   dependant  = filename ":" <SP> "#deps" <SP> <count> ","
  #                <SP> "deps" <SP> "mtime" <SP> <milliseconds> <note> <LF>
  #   dependency = <SP>{4} filename
  #
  # Example:
  #   CMakeFiles/hamt.dir/hamt.c.o: #deps 178, deps mtime 1621036073929710506 (VALID)
  #       ~/ckit/pkg/rbase/rbase.h
  #       ~/ckit/pkg/hamt/hamt.h
  #
  mkdir -p "$BUILD_DIR"
  _pushd "$BUILD_DIR"
  "$BUILD_TOOL" "$BUILD_TARGET" -t deps \
    | grep -E "^    ${SRC_DIR//\./\\.}/|    \." \
    | sed -e 's/^    //' \
    | grep -v "^$BUILD_DIR" \
    | sed -e 's@^../../@'"$SRC_DIR/"'@' \
    >> "$WATCH_FILES_FILE" || true
  _popd

  WATCH_FILES=( $(sort -u "$WATCH_FILES_FILE") )
  WATCH_FILES+=( "$SRC_DIR/cmakelists.txt" )
  if [ ${#EXTRA_WATCH_FILES[@]} -gt 0 ]; then
    WATCH_FILES+=( "${EXTRA_WATCH_FILES[@]}" )
  fi

  if $VERBOSE; then
    echo "watching files:"
    for f in "${WATCH_FILES[@]}"; do
      echo "  $(_nicedir "$f")"
    done | $LIST_FORMATTER
  fi

  # start animated watch indicator message "Waiting for changes to files..."
  # and watch files in WATCH_FILES for changes
  _watch_indicator &
  echo $! > "$WATCH_INDICATOR_PIDFILE"
  _watch_files_for_changes
  _watch_indicator_stop
  echo "———————————————————— restarting ————————————————————"

done  # while true
