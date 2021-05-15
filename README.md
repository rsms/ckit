# ckit

The little C kit

- `bin/ckit` — tool for building and testing projects (calls cmake & ninja)
- `pkg/` — directory of some packages
  - The main thing here is `rbase` — basic functionality for C projects
- `ckit.cmake` — CMake helper file


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
ckit init   # creates cmakelists.txt
ckit build  # produces out/debug/myproject (or .a if there's no main)
./out/debug/myproject
ckit test   # builds & runs out/debug-test/myproject-test
```


Example of ckit as a subdirectory:

```sh
mkdir ~/myproject
cd ~/myproject
git clone https://github.com/rsms/ckit.git
echo "int main() { return 0; }" > hello.c
./ckit/bin/ckit init   # creates cmakelists.txt
./ckit/bin/ckit build  # produces out/debug/myproject (or .a if there's no main)
./out/debug/myproject
./ckit/bin/ckit test   # builds & runs out/debug-test/myproject-test
```


Note that you don't have to use `bin/ckit`.
ckit packages are plain CMake projects and thus you can just use CMake if you want.
