#!/bin/sh
cd "$(dirname "$0")"
set -e

GIT_REPO=https://github.com/ianlancetaylor/libbacktrace.git

rm -rf backtrace-new
git clone "$GIT_REPO" backtrace-new
cp -a backtrace/cmakelists.txt backtrace-new/
cp -a backtrace/config.h.in.cmake backtrace-new/

CONFIG_DIFFERS=false
if [ "$(diff -u backtrace3/config.h.in backtrace/config.h.in)" != "" ]; then
  diff -u backtrace/config.h.in backtrace-new/config.h.in > backtrace-new/config.h.in.diff
  CONFIG_DIFFERS=true
fi

# save git info and remove git repo state
GITHASH_SHORT=$(git -C backtrace-new rev-parse --short HEAD)
echo "$GIT_REPO" > backtrace-new/source.txt
git -C backtrace-new log --pretty=medium -n1 | head -n3 >> backtrace-new/source.txt
rm -rf backtrace-new/.git

# run libbacktrace test suite in a copy of the source dir.
# Disable by setting SKIP_TEST=1 in env.
if [ -z "$SKIP_TEST" ]; then
  cp -a backtrace backtrace-new-test
  cd backtrace-new-test
  ./configure
  make -j$(nproc) check || true
  cat << _EOF_
The following tests are expected to FAIL on macOS:
- btest_alloc
- btest
- edtest
- edtest_alloc
- dwarf5
- dwarf5_alloc
- mtest
_EOF_
  cd ..
  rm -rf backtrace-new-test
fi

# remove unused files
cd backtrace-new
rm -rf config
rm \
  *.m4 \
  *.sh \
  *test* \
  compile \
  config.guess \
  config.sub \
  configure \
  configure.ac \
  filetype.awk \
  install-debuginfo-for-buildid.sh.in \
  install-sh \
  instrumented_alloc.c \
  Isaac.Newton-Opticks.txt \
  Makefile* \
  missing \
  move-if-change \

cd ..

# stage
rm -rf backtrace-prev
mv backtrace backtrace-prev
mv backtrace-new backtrace
echo "Okay, libbacktrace has been updated to:"
echo "-------------------------------------------------------------------------"
cat backtrace/source.txt
echo "-------------------------------------------------------------------------"
echo "The previous version has moved to $PWD/backtrace-prev"
echo ""
if $CONFIG_DIFFERS; then
  echo ""
  echo "Backtrace config changed!" >&2
  echo "Please review backtrace/config.h.in.diff" >&2
  echo "and update backtrace/config.h.in.cmake & backtrace/cmakelists.txt" >&2
  echo ""
fi
cat << _EOF_
You should test things out now:
  cd '$PWD'
  ckit test -clean

If you'd like to proceed with the upgrade:
  rm -rf '$PWD/backtrace-prev'
  git add '$PWD/backtrace'
  git commit -m 'rbase: upgrade backtrace to (git ${GITHASH_SHORT})' '$PWD/backtrace'

If you'd like to undo this upgrade:
  rm -rf '$PWD/backtrace'
  mv '$PWD/backtrace-prev' '$PWD/backtrace'

_EOF_
