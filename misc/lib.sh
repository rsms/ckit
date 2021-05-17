#!/bin/sh
set -e
OUTER_PWD=$PWD
COMMAND=
EXIT_CODE=0
VERBOSE=false
CKIT_EXE=$0
PROG=$(basename "$CKIT_EXE")
ATEXIT=()
TMPFILES_LIST="$WORK_DIR/tmp/tmpfiles.$$"

__atexit() {
  set +e
  cd "$OUTER_PWD" # in case PWD is a dir being unmounted or deleted
  # execute each command in stack order (FIFO)
  local idx
  for (( idx=${#ATEXIT[@]}-1 ; idx>=0 ; idx-- )) ; do
    # echo "[atexit]" "${ATEXIT[idx]}"
    eval "${ATEXIT[idx]}"
  done
  # clean up temporary files
  if [ -f "$TMPFILES_LIST" ]; then
    while IFS= read -r f; do
      # echo "[atexit]" rm -rf "$f"
      rm -rf "$f"
    done < "$TMPFILES_LIST"
    rm -f "$TMPFILES_LIST"
  fi
  set -e
}

_onsigint() {
  echo
  exit
}

trap __atexit EXIT
trap _onsigint SIGINT

# _tmpfile
# Prints a unique filename that can be written to, which is automatically
# deleted when the script exits.
_tmpfile() {
  mkdir -p "$WORK_DIR/tmp"
  local file=$(mktemp -t "ckit-$$")
  mkdir -p "$(dirname "$TMPFILES_LIST")"
  echo "$file" >> "$TMPFILES_LIST"
  echo "$file"
}

# _pidfile_kill <pidfile>
_pidfile_kill() {
  local pidfile="$1"
  # echo "_pidfile_kill $1"
  if [ -f "$pidfile" ]; then
    local pid=$(cat "$pidfile" 2>/dev/null)
    # echo "_pidfile_kill pid=$pid"
    [ -z "$pid" ] || kill $pid 2>/dev/null || true
    rm -f "$pidfile"
  fi
}

_prog_cmd() {
  if [ -n "$COMMAND" ]; then
    echo "$PROG $COMMAND"
  else
    echo "$PROG"
  fi
}

_err() {
  echo "$(_prog_cmd): $@" >&2
  exit 1
}

_errhelp() {
  echo "$(_prog_cmd): $@" >&2
  echo "See $(_prog_cmd) -help for details" >&2
  exit 1
}

_nicedir() {
  case "$1" in
    "$OUTER_PWD"/*) echo ".${1:${#OUTER_PWD}}" ;;
    "$OUTER_PWD")   echo "." ;;
    "$HOME"/*)      echo "~${1:${#HOME}}" ;;
    *)              echo "$1" ;;
  esac
}

_pushd() {
  pushd "$1" >/dev/null
  _logv "cd $(_nicedir "$PWD")"
}

_popd() {
  popd >/dev/null
  _logv "cd $(_nicedir "$PWD")"
}

_expectarg() {
  [ -n "$2" ] || _err "missing value for $1"
}

# egrep
if ! command -v egrep >/dev/null; then
  egrep() {
    grep -E "$@"
    return $?
  }
fi

_logv() {
  if $VERBOSE; then
    echo "$@" >&2
  fi
}

# _build_dir_suffix <build-type> [<enable-tests>]
_build_dir_suffix() {
  local suffix=$1
  [ -z "$2" ] || suffix=$suffix-test
  echo "$suffix"
}

# _cmake_build_type <build-type>
_cmake_build_type() {
  case "$1" in
    fast)  echo Release ;;
    safe)  echo ReleaseSafe ;;
    debug) echo Debug ;;
    *)     echo "$1" ;;
  esac
}
