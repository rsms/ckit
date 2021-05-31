
_help() {
  cat <<- _EOF_ >&2
Generate cmakelists.txt
Usage: $PROG $COMMAND [options] [<dir> [<srcdir>]]
options:
$(_help_common_options)
<dir>    Project directory. Defaults to the current directory.
<srcdir> Source directory to scan for sources files. Defaults to <dir>.
_EOF_
}

# _file_has_cmain <file>
_file_has_cmain() {
  local cmd=egrep
  if command -v pcregrep >/dev/null; then
    cmd="pcregrep -M"
  fi
  $cmd -q 'int[ \t\r\n]+main[ \t]*\(' "$1"
  return $?
}


# _init_pkg [<pkg> [<srcdir>]]
_init_pkg() {
  local pkg=${1:-.}
  local srcdir=${2:-$pkg}

  [ -e "$pkg" ] || _err "$pkg not found"
  [ -d "$pkg" ] || _err "$pkg is not a directory"
  [ ! -f cmakelists.txt ] || _err "$pkg/cmakelists.txt already exists"
  local pkgabsdir=$(realpath "$pkg")
  local pkgname=$(basename "$pkgabsdir")
  local srcdir=$pkgabsdir

  # process srcdir arg
  if [ "$srcdir" == "$pkg" ] || [ "$srcdir" == "." ]; then
    srcdir=$pkgabsdir
  else
    # make sure that srcdir is a subdirectory of pkg
    local srcdirabs=$(realpath "$srcdir")
    case "$srcdirabs" in
      "$pkgabsdir"|"$pkgabsdir"/*) ;;
      *) _err "<srcdir> is not a subdirectory of <pkg> ($srcdir !< $pkg)" ;;
    esac
    srcdir=$srcdirabs
  fi

  # generate source file list
  rm -f .$PROG-srcfiles.tmp
  touch .$PROG-srcfiles.tmp
  # find twice to make files in srcdir appear first
  find "$srcdir" -type f -depth 1 -name '*.c' | sort -h > .$PROG-srcfiles1.tmp
  find "$srcdir" -type f -mindepth 2 -maxdepth 4 -name '*.c' \
  | sort -h \
  | grep -v "${CKIT_DIR}" \
  >> .$PROG-srcfiles1.tmp || true
  local prefix="$pkgabsdir/"
  local found_source_files=false
  local mainfiles=()
  local fn
  while IFS= read -r f; do
    fn=${f:${#prefix}}
    if _file_has_cmain "$f"; then
      mainfiles+=( "$fn" )
    fi
    case "$fn" in
      *" "*) f='"'"$fn"'"' ;;  # quote paths with spaces
    esac
    echo "  $fn" >> .$PROG-srcfiles.tmp
    found_source_files=true
  done < .$PROG-srcfiles1.tmp
  rm .$PROG-srcfiles1.tmp
  if ! $found_source_files; then
    echo "  # add source files here" > .$PROG-srcfiles.tmp
  fi

  # check main files
  local cmake_add_function=add_library
  local sp_mainfile
  if [ ${#mainfiles[@]} -gt 0 ]; then
    cmake_add_function=add_executable
    if [ ${#mainfiles[@]} -gt 1 ]; then
      echo "Warning: multiple files with main() functions: ${mainfiles[@]}" >&2
      echo "Only using ${mainfiles[0]}"
      if [ "$srcdir" == "$pkgabsdir" ]; then
        echo "Tip: you can specify a specific source subdirectory. See $PROG -help"
      fi
      # TODO: consider main files per (sub)directory.
    fi
    sp_mainfile=" ${mainfiles[0]}"
    echo "Configuring as executable with main source file ${mainfiles[0]}"
  else
    echo "Configuring as library"
  fi

  # generate cmakelists.txt
  cat << _EOF_ > cmakelists.txt
cmake_minimum_required(VERSION 3.12)
project($pkgname  VERSION 0.1.0  LANGUAGES C)

include(\$ENV{CKIT_DIR}/ckit.cmake)
ckit_configure_project(C)
ckit_require_package(rbase)

$cmake_add_function(\${PROJECT_NAME}
$(cat .$PROG-srcfiles.tmp)
)
target_link_libraries(\${PROJECT_NAME} rbase)

ckit_define_test(\${PROJECT_NAME}${sp_mainfile})
_EOF_

  # sed -E -e "s/@@PROJECT@@/$pkgname/" "$CKIT_DIR"/misc/pkg-template.cmake |
  # sed -E -e '/@@SOURCEFILES@@/{
  #   s/@@SOURCEFILES@@//
  #   r .$PROG-srcfiles.tmp
  # }' > cmakelists.txt

  rm .$PROG-srcfiles.tmp
  echo "Created $(_nicedir "$pkg/cmakelists.txt")"
}

# -----------------------------------------------------------------------------------------------
# main

while [[ $# -gt 0 ]]; do case "$1" in
  --)
    shift
    break
    ;;
  -*)
    set +e ; _parse_common_option "$@" ; N=$? ; set -e
    for i in `seq $N`; do shift; done # consume args
    ;;
  *)
    break
    ;;
esac; done

[ -z "$3" ] || _err "Unexpected extra argument $3"

_init_pkg "$1" "$2"
