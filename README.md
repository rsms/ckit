# ckit

The little C kit

- `bin/ckit` — tool for building and testing projects (calls cmake & ninja)
- `pkg/rbase` — common functionality for C projects (optional to use)
- `example/hello/` — an example project
- `ckit.cmake` — CMake routines

Quick install: (see [Installing](#installing) for details and options)

```sh
git clone https://github.com/rsms/ckit.git ~/ckit
cat << EOF >> ~/`[[ $SHELL == *"/zsh" ]] && echo .zshrc || echo .bashrc`
export CKIT_DIR=\$HOME/ckit
export PATH=\$PATH:\$CKIT_DIR/bin
EOF
```


## `bin/ckit`

This multitool provides a few convenience commands:

- `ckit build` or `ckit` — build project of current directory
- `ckit test` — build & run tests
- `ckit watch` — build & run as sources files change
- `ckit init` — generate `cmakelists.txt` file for a project

See `ckit -help` for details or [have a look at an example project](example/hello)


## Installing

ckit can be installed in two ways:
- Placed in a shared location, for example `~/ckit`
- "Vendored" as a subfolder in a project, for example `myproject/ckit`

Example of ckit installed in a shared location:

```sh
git clone https://github.com/rsms/ckit.git ~/ckit
export CKIT_DIR=$HOME/ckit
export PATH=$PATH:$CKIT_DIR/bin
mkdir ~/myproject
cd ~/myproject
echo "int main() { return 0; }" > hello.c
ckit init                # generate cmakelists.txt
ckit build               # build all targets
./out/debug/myproject    # run example program
ckit test                # build & run tests
ckit watch -r myproject  # build & run as sources change
```


Example of ckit as a subdirectory:

```sh
mkdir ~/myproject
cd ~/myproject
git clone https://github.com/rsms/ckit.git
echo "int main() { return 0; }" > hello.c
./ckit/bin/ckit init                # generate cmakelists.txt
./ckit/bin/ckit build               # build all targets
./out/debug/myproject               # run example program
./ckit/bin/ckit test                # build & run tests
./ckit/bin/ckit watch -r myproject  # build & run as sources change
```

Dependencies:
- modern C shell like zsh, bash, ash or hush
- modern C compiler like clang 10 or GCC 10 (one that supports C11)
- [ninja](https://ninja-build.org)
- [cmake](https://cmake.org) >=3.12

Additionally, the `ckit watch` command requires:
- [ninja](https://ninja-build.org) version >=1.9 (for `ninja -t deps`)
- [fswatch](https://github.com/emcrisostomo/fswatch)
  OR [inotify-tools](https://github.com/inotify-tools/inotify-tools)

Note that you don't have to use `bin/ckit`.
ckit packages are plain CMake projects and thus you can just use CMake if you want.


## Notes

Some CMake reading:
- [Modern CMake](https://cliutils.gitlab.io/modern-cmake/)
- [Effective Modern CMake](https://gist.github.com/mbinna/c61dbb39bca0e4fb7d1f73b0d66a4fd1)
