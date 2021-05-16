BUILD_TOOL_ARGS=()
OPT_BUILD_DIR=
OPT_RELEASE=false
OPT_TEST=
OPT_CLEAN=false

_cmd_common_options() {
  cat <<- _EOF_
  -clean      Build from scratch.
  -bt <arg>   Provide arg to build tool. ($BUILD_TOOL)
$(_help_common_options)
  --          Stop processing options.
_EOF_
}

_help_build_pkg() {
  cat <<- _EOF_ >&2
Build packages
Usage: $PROG $COMMAND [options] [<pkg> ...]
options:
  -O, -release  Build release variant instead of debug variant.
  -T, -test     Build with tests enabled.
  -B <dir>      Specify explicit build directory. Overrides -T and -O.
$(_cmd_common_options)
<pkg>
  Either the name of a package in $(_nicedir "$CKIT_DIR/pkg") or a directory path.
  Defaults to the current working directory if not provided.
_EOF_
}

_help_build() {
  cat <<- _EOF_ >&2
Build current package
Usage: $PROG $COMMAND [options] [<$BUILD_TOOL-arg> ...]
options:
  -O, -release  Build release variant instead of debug variant.
  -T, -test     Build with tests enabled.
  -B <dir>      Specify explicit build directory. Overrides -T and -O.
$(_cmd_common_options)
<$BUILD_TOOL-arg>
  Arguments to $BUILD_TOOL, like build target names defined in the
  project's cmakelists.txt file. If not set, the project's default
  target is built (usually "all").
_EOF_
}

_help_test() {
  cat <<- _EOF_ >&2
Build & test package
Usage: $PROG $COMMAND [options] [<filter>]
options:
  -B <dir>      Specify explicit build directory.
$(_cmd_common_options)
<filter>
  Only run R_TESTs which name starts with <filer>
  Sets environment variable R_TEST_FILTER.
_EOF_
}

_help() {
  case "$COMMAND" in
    build-pkg) _help_build_pkg ;;
    test)      _help_test ;;
    build)     _help_build ;;
    *)         _err "unexpected COMMAND=$COMMAND"
  esac
}

# _build_pkg <pkg> [build tool args]
_build_pkg() {
  local pkg=$1 ; shift

  # resolve SRC_DIR directory
  local SRC_DIR=$pkg
  if [ ! -d "$SRC_DIR" ]; then
    case "$SRC_DIR" in
      ./*|/*)
        _err "$SRC_DIR is not a directory"
        ;;
      *)
        SRC_DIR=$CKIT_DIR/pkg/$pkg
        [ -d "$SRC_DIR" ] ||
          _err "package \"$pkg\" not found in $(_nicedir "$CKIT_DIR/pkg")"
        ;;
    esac
  fi
  SRC_DIR=$(realpath "$SRC_DIR")
  pkg=$(basename "$SRC_DIR")

  # make sure this is a project
  [ -f "$SRC_DIR"/cmakelists.txt ] ||
    _err "Directory $(_nicedir "$SRC_DIR") is not a package (no cmakelists.txt file)"

  # select build directory
  local BUILD_DIR=$OPT_BUILD_DIR
  if [ -z "$BUILD_DIR" ]; then
    case "$SRC_DIR" in
      "$CKIT_DIR/pkg/"*) BUILD_DIR=$CKIT_DIR/out/$pkg ;;
      *)                  BUILD_DIR=$PWD/out ;;
    esac
  fi

  # build mode affects BUILD_DIR and CMAKE_BUILD_TYPE
  # _build_dir_suffix <build-type> <enable-tests>
  CMAKE_BUILD_TYPE=Debug
  if $OPT_RELEASE; then
    CMAKE_BUILD_TYPE=Release
  fi
  CMAKE_ARGS+=( -DCMAKE_BUILD_TYPE="$CMAKE_BUILD_TYPE" )
  BUILD_DIR=$BUILD_DIR/$(_build_dir_suffix "$CMAKE_BUILD_TYPE" $OPT_TEST)

  # enable tests
  if [ -n "$OPT_TEST" ]; then
    CMAKE_ARGS+=( -DTEST_ENABLED=on )
  elif [ "$1" == "test" ]; then
    echo "$PROG: warning: target \"test\" without tests enabled." >&2
    echo "Use the -T argument or \`$PROG test\` to enable tests."  >&2
  fi

  _logv "$COMMAND $pkg in $(_nicedir "$BUILD_DIR")"

  # clean?
  if $OPT_CLEAN; then rm -rf "$BUILD_DIR"; fi
  mkdir -p "$BUILD_DIR"

  # support globbed source files; let cmake know if the list changes
  local globpat=$(grep -E -v '^[ \t]*#' "$SRC_DIR"/cmakelists.txt \
                | grep -E '\bfile\(GLOB[^\)]+\)\b' \
                | sed -E -e 's/^[^"]+"([^"]+)"\)/\1/')
  if [ -n "$globpat" ]; then
    local SRC_SHASUM=$(ls $globpat | sha1sum)
    if [ "$(cat "$BUILD_DIR"/rglobsrc.checksum 2>/dev/null)" != "$SRC_SHASUM" ]; then
      echo "$SRC_SHASUM" > "$BUILD_DIR"/rglobsrc.checksum
      if [ -f "$BUILD_DIR"/CMakeCache.txt ]; then
        touch "$BUILD_DIR"/CMakeCache.txt
      fi
    fi
  fi

  # (re)generate build config
  if [ ! -f "$BUILD_DIR"/CMakeCache.txt ] || [ ! -f "$BUILD_DIR"/build.ninja ]; then
    _pushd "$SRC_DIR"
    cmake -B "$BUILD_DIR" -GNinja "${CMAKE_ARGS[@]}" .
    _popd
  fi

  # invoke build tool
  _pushd "$BUILD_DIR"
  _logv $BUILD_TOOL "${BUILD_TOOL_ARGS[@]}" "$@"
  $BUILD_TOOL "${BUILD_TOOL_ARGS[@]}" "$@"
  local status=$?
  _popd

  return $status
}


# -----------------------------------------------------------------------------------------------
# main

HELP=false
REST_ARGS=()

# Note: cmd-watch.sh duplicates a subset of the following code.
# If you make changes here, make sure to also check cmd-watch.sh.
while [[ $# -gt 0 ]]; do case "$1" in
  -O|-release) OPT_RELEASE=true; shift ;;
  -T|-test)    OPT_TEST=y; shift ;;
  -clean)      OPT_CLEAN=true; shift ;;
  -B)          _expectarg "$1" "$2"; OPT_BUILD_DIR="$2"; shift; shift; break ;;
  -bt)         _expectarg "$1" "$2"; BUILD_TOOL_ARGS+=("$2"); shift; shift; break ;;
  --)          shift; REST_ARGS+=( "$@" ); break ;;
  -*)
    set +e ; _try_parse_common_option "$@" ; N=$? ; set -e
    if [ $N -eq 0 ]; then
      if [ "$COMMAND" == "build" ]; then
        # unknown options are forwarded to the build tool
        REST_ARGS+=("$@")
        break
      fi
      _errhelp "Unknown option $1"
    fi
    # else, consume args
    for i in `seq $N`; do shift; done
    ;;
  *) REST_ARGS+=( "$1" ); shift ;;
esac; done

if $VERBOSE; then
  BUILD_TOOL_ARGS+=( -v )
fi

case "$COMMAND" in
  build-pkg)
    [ ${#REST_ARGS[@]} -gt 0 ] || REST_ARGS=(.)
    for pkg in "${REST_ARGS[@]}"; do
      if ! _build_pkg "$pkg" ; then
        [ ${#REST_ARGS[@]} -eq 1 ] || echo "$COMMAND $pkg failed" >&2  # log when many
        EXIT_CODE=1
      fi
    done
    ;;

  build)
    _build_pkg . "${REST_ARGS[@]}"
    ;;

  test)
    OPT_TEST=y
    # <filter> ?
    if [ ${#REST_ARGS[@]} -gt 0 ]; then
      export R_TEST_FILTER="${REST_ARGS[0]}"
      if [ ${#REST_ARGS[@]} -gt 1 ]; then
        _errhelp "Unexpected extra argument(s): ${REST_ARGS[@]:1}"
      fi
      REST_ARGS=()
    fi
    _build_pkg . test
    ;;
esac
