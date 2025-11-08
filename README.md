# yoMMD

yoMMD is a software to show MMD models on Desktop, as known as "Desktop mascot,"
for Windows and macOS.

![Screenshot](https://github.com/mityu/yoMMD/assets/24771416/cefd0730-be00-42d7-abdc-b9a2ba64f89e)

- MMD Model: つみ式ミクさんv4 by つみだんご様 (https://3d.nicovideo.jp/works/td56081)
- Motion: ぼんやり待ちループ by むつごろう様 (https://3d.nicovideo.jp/works/td30405)

# Pre-build binary

For Windows user, you can download pre-build binary from here: https://github.com/mityu/yoMMD/releases/latest

# Development environment

- Apple/homebrew clang on macOS Ventura 13
- GCC on MinGW64 on MSYS2 on Windows 11

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

Only MinGW64 on MSYS2 is officially supported now.  Please prepare it before
building yoMMD.

Note that you have to use `mingw-w64-x86_64-cmake` package, not `cmake` package.
yoMMD build will fail with the msys cmake package.

```
$ pacman -S make
$ pacman -S mingw-w64-x86_64-cmake
$ pacman -S mingw-w64-x86_64-ninja  # optional
```


# Build

yoMMD manages its all dependencies as submodule.  You don't need to additionaly
install any libraries.

```
$ git clone https://github.com/mityu/yoMMD
$ cd yoMMD
$ make init-submodule # Initialize submodule. This takes a bit long time...
$ make build-submodule
$ make release -j4  # Build ./release/yoMMD executable
```

### On macOS
If you want to create application bundle (`yoMMD.app`), additionaly do:

```
$ make app
```

`yoMMD.app` will available under `package/` directory.


## Debug build

If you want to build debug info included binary (and with DOS window enabled on Windows), just do:

```
$ make
```

or explicitly pass target name:

```
$ make debug
```

This will build yoMMD binary with debug information at `./yoMMD` (or `./yoMMD.exe` on Windows).

See `$ make help` result for other available subcommands.

# Usage, configuration

Please see files under `doc/` directory: https://github.com/mityu/yoMMD/tree/main/doc

# Example configuration

The example configuration for the screenshot on the top of this README.

Note that if you copy and paste this configuration on Windows, you may encounter an weired error that says no model or motion is found on the path `./つみ式ミクさんv4/つみ式ミクさんv4.pmx` or `./ぼんやり待ちループ/ぼんやり待ち合わせ_腕広いver(465f).vmd` even if it looks like existing there.  It is due to character encoding error, and in that case, please try re-typing the path manually or copying and pasting file path from actual model or motion file on your computer.

```toml
model = "./つみ式ミクさんv4/つみ式ミクさんv4.pmx"
default-model-position = [0.65, -0.85]
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
