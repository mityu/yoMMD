# yoMMD

yoMMD is a software to show MMD models on Desktop, as known as "Desktop mascot,"
for Windows and macOS.

![Screenshot](https://github.com/mityu/yoMMD/assets/24771416/cefd0730-be00-42d7-abdc-b9a2ba64f89e)

- MMD Model: つみ式ミクさんv4(https://3d.nicovideo.jp/works/td56081)
- Motion: ぼんやり待ちループ(https://3d.nicovideo.jp/works/td30405)

# Pre-build binary

For windows user, you can download pre-build binary from here: https://github.com/mityu/yoMMD/releases/latest

# Development environment

- Apple/homebrew clang on macOS Ventura 13
- GCC on MinGW64 on MSYS2 on Windows 10

yoMMD must work on these environment.

# Build tools

- gcc (Windows)/clang (macOS)
- make
- cmake
- (optional) ninja

## Installation

#### With homebrew:

```
$ brew install cmake
$ brew install ninja  # optional
```

#### With MinGW64 on msys2:

Note that you have to use `mingw-w64-x86_64-cmake` package, not `cmake` package.
yoMMD build will fail with the msys cmake package.

```
$ pacman -S mingw-w64-x86_64-cmake
$ pacman -S mingw-w64-x86_64-ninja  # optional
```


# Build

yoMMD manages its all dependencies as submodule.  You don't need to additionaly
install any libraries.

## On macOS

```
$ git clone https://github.com/mityu/yoMMD
$ cd yoMMD
$ git submodule update --init --recursive  # Initialize submodule. This takes a bit long time...
$ make build-submodule
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
$ git submodule update --init --recursive  # Initialize submodule. This takes a bit long time...
$ make build-submodule
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

# Example configration

The example configuration for the screenshot on the top of this README.

```toml
model = "./つみ式ミクさんv4/つみ式ミクさんv4.pmx"
default-model-position = [0.65, 0.85]
default-camera-position = [0.0, 20.0, 50.0]
default-scale = 2.0

[[motion]]
path = ["./ぼんやり待ちループ/ぼんやり待ち合わせ_腕広いver(465f).vmd"]

# To specify multiple .vmd files.
# [[motion]]
# path = ["./path/to/awesome_model_motion.vmd", "./path/to/awesome_camera_motion.vmd"]
# disabled = true  # Make this "true" to disable this motion.
```

where directory structure is:

```
|-- config.toml
|-- つみ式ミクさんv4
|    |-- ...
|    |-- つみ式ミクさんv4.pmx
+-- ぼんやり待ちループ
     |-- ...
     |-- ぼんやり待ち合わせ_腕広いver(465f).vmd
```

# License

The MIT License, except for the files in `icons/` directory.
For licenses of external libraries, please see [LICENSES.md](./LICENSES.md)


# Acknowledgements

- This software is powered by many awesome external libraries placed under
  `lib` directory.  Thank you.
- MMD drawer program, especially the shader program, is ported from
  `saba_viewer` program in
  [benikabocha/Saba](https://github.com/benikabocha/Saba) library. Thank you.
