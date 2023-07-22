# yoMMD

yoMMD is a software to show MMD models on Desktop, as known as "Desktop mascot,"
for Windows and macOS.

# Development environment

- Apple/homebrew clang on macOS Ventura 13
- GCC on MinGW64 on MSYS2 on Windows 10

yoMMD must work on these environment.


# Build

The dependencies are managed as submodule under `lib/` and you don't need to
set up external libraries before.

## On macOS

```
$ git clone https://github.com/mityu/yoMMD
$ cd yoMMD
$ make init-submodule  # Initialize submodule and build them. This takes a bit long time...
$ make -j4  # Build ./yoMMD executable
```

If you want to create application bundle (`yoMMD.app`), additionaly do:

```
$ make app
```

`yoMMD.app` will available under `package/` directory.

## On Windows

Only MinGW64 on MSYS2 is officially supported now.  Please prepare it before
building yoMMD.

```
$ git clone https://github.com/mityu/yoMMD
$ cd yoMMD
$ make init-submodule  # Initialize submodule and build them. This takes a bit long time...
$ make release -j4  # Build ./yoMMD.exe executable.
```

If you want to build binary with DOS window enabled for some purpose (e.g.
debugging), just do:

```
$ make
```

See `$ make help` result for other available subcommands.

# Configuration

You can write configurations in `config.toml`.

TODO: Write more

# FAQ

Q. Is this work with multi monitors?
A. I don't know since I don't use multi monitors.

# License

The MIT License, except for the files in `icons/` directory.
For licenses of external libraries, please see [LICENSES.md](./LICENSES.md)


# Acknowledgements

- This software is powered by many awesome external libraries placed under
  `lib` directory.  Thank you.
- MMD drawer program, especially the shader program, is ported from
  `saba_viewer` program in
  [benikabocha/Saba](https://github.com/benikabocha/Saba) library. Thank you.
