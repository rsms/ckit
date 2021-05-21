This is an example project.

```
cd example/hello
ckit watch test
```

## Things to try

Build in debug mode and run the product:

```
$ ckit
[3/3] Linking C executable hello
./out/debug/hello sam 123
argv[0] = ./out/debug/hello (main)
person's name:   "sam" (3 bytes)
person's number: 123
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
