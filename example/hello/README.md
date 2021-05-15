This is an example project.

```
cd example/hello
```

## Things to try

Build in debug mode and run the product:

```
$ ckit
[3/3] Linking C executable hello
./out/debug/hello
argv[0] = ./out/debug/hello (main)
usage: hello name number
```

Build in release mode:

```
$ ckit -release
```

Build & run tests: (defined with `R_TEST` in source files)

```
$ ckit test
```

Build & run tests which names starts with "Person":

```
$ ckit test Person
```

Watch source files for changes, rebuild as they do and run the program:

```
$ ckit watch hello -run
```

Watch test source files for changes, rebuild as they do and run all tests:

```
$ ckit watch test
```

Watch test source files for changes and run tests which names starts with "Person":

```
$ ckit watch test Person
```
