CXX:=g++
CC:=gcc
TARGET:=yoMMD
TARGET_DEBUG:=yoMMD-debug
OBJDIR:=./obj
SRC:=viewer.cpp config.cpp resources.cpp image.cpp keyboard.cpp util.cpp libs.mm
OBJ=$(SRC:%=$(OBJDIR)/%.o) $(OBJDIR)/version.cpp.o
DEP=$(OBJ:%.o=%.d)
CFLAGS:=-O2 -Ilib/saba/src/ -Ilib/sokol -Ilib/glm -Ilib/stb \
		-Ilib/toml11/include -Ilib/incbin -Ilib/bullet3/build/include/bullet \
		-Wall -Wextra -pedantic -MMD -MP
CPPFLAGS=-std=c++20
OBJCFLAGS:=
LDFLAGS:=-Llib/saba/build/src -lSaba -Llib/bullet3/build/lib \
		 -lBulletDynamics -lBulletCollision -lBulletSoftBody -lLinearMath
PKGNAME_PLATFORM:=
CMAKE_GENERATOR:=
CMAKE_BUILDFILE:=Makefile

ifeq ($(OS),Windows_NT)
TARGET:=$(TARGET).exe
TARGET_DEBUG:=$(TARGET_DEBUG).exe
SRC+=main_windows.cpp resource_windows.rc
CFLAGS+=-Wno-missing-field-initializers
LDFLAGS+=-static -lkernel32 -luser32 -lshell32 -ld3d11 -ldxgi -ldcomp -lgdi32 -ldwmapi -municode
SOKOL_SHDC:=lib/sokol-tools-bin/bin/win32/sokol-shdc.exe
PKGNAME_PLATFORM:=win-x86_64
CMAKE_GENERATOR:=-G "MSYS Makefiles"
else ifeq ($(shell uname),Darwin)
CXX:=clang++
CC:=clang
SRC+=main_osx.mm
LDFLAGS+=-F$(shell xcrun --show-sdk-path)/System/Library/Frameworks  # Homebrew clang needs this.
LDFLAGS+=-framework Foundation -framework Cocoa -framework Metal -framework MetalKit -framework QuartzCore
OBJCFLAGS:=-fobjc-arc
ifeq ($(shell uname -m),arm64)
SOKOL_SHDC:=lib/sokol-tools-bin/bin/osx_arm64/sokol-shdc
PKGNAME_PLATFORM:=darwin-arm64
else
SOKOL_SHDC:=lib/sokol-tools-bin/bin/osx/sokol-shdc
PKGNAME_PLATFORM:=darwin-x86_64
endif
endif

ifneq ($(shell command -v ninja),)
CMAKE_GENERATOR:=-G Ninja
CMAKE_BUILDFILE:=build.ninja
endif

$(TARGET): $(OBJDIR) $(OBJ)
	$(CXX) -o $@ $(OBJ) $(LDFLAGS)

ifeq ($(OS),Windows_NT)
release:
	@[ ! -f "$(TARGET)" ] || rm $(TARGET)
	@$(MAKE) LDFLAGS="$(LDFLAGS) -mwindows"

may-create-release-build:
	@(read -p "Make release build? [Y/n] " yn && [ $${yn:-N} = y ]) \
		|| exit 0 && $(MAKE) release
else
may-create-release-build:
	@ # Nothing to do.
endif

debug: CFLAGS+=-g -O0
debug: OBJDIR:=$(OBJDIR)/debug
debug: TARGET:=$(TARGET_DEBUG)
debug:
	@$(MAKE) CFLAGS="$(CFLAGS)" OBJDIR="$(OBJDIR)" TARGET="$(TARGET)"

-include $(DEP)

$(OBJDIR)/version.cpp.o: auto/version.cpp
	$(CXX) -o $@ $(CPPFLAGS) $(CFLAGS) -c $<

ifneq ($(shell uname),Darwin)
# When not on macOS, compile libs.mm as C program.
$(OBJDIR)/libs.mm.o: libs.mm
	$(CC) -o $@ $(CFLAGS) -c -x c $<
endif

$(OBJDIR)/viewer.cpp.o: auto/yommd.glsl.h auto/quad.glsl.h
$(OBJDIR)/%.cpp.o: %.cpp
	$(CXX) -o $@ $(CPPFLAGS) $(CFLAGS) -c $<

$(OBJDIR)/%.mm.o: %.mm
	$(CXX) -o $@ $(CPPFLAGS) $(OBJCFLAGS) $(CFLAGS) -c $<

$(OBJDIR)/resource_windows.rc.o: resource_windows.rc DpiAwareness.manifest
	windres -o $@ $<

auto/%.glsl.h: %.glsl $(SOKOL_SHDC) | auto/
	$(SOKOL_SHDC) --input $< --output $@ --slang metal_macos:hlsl5
ifeq ($(OS),Windows_NT)
	# CRLF -> LF
	tr -d \\r < $@ > $@.tmp && mv $@.tmp $@
endif

.PHONY: FORCE-EXECUTE
auto/version.cpp: FORCE-EXECUTE
	./scripts/gen-version-cpp

run: $(TARGET)
	./$(TARGET)

clean:
	$(RM) $(TARGET_DEBUG) $(OBJDIR)/debug/*.o $(OBJDIR)/debug/*.d
	$(RM) $(OBJDIR)/*.o $(OBJDIR)/*.d $(TARGET)
	$(RM) auto/*

all: clean $(TARGET);

$(OBJDIR) auto/ tool/:
	mkdir -p $@

# Make distribution package
PKGNAME:=yoMMD-$(PKGNAME_PLATFORM)-$(shell date '+%Y%m%d%H%M').zip
package: may-create-release-build
	@[ -d "package" ] || mkdir package
	zip package/$(PKGNAME) $(TARGET)

package-huge: package
	@[ -d "default-attachments" ] && cd default-attachments && \
		zip -ur ../package/$(PKGNAME) \
		$(notdir $(wildcard default-attachments/*)) -x "*/.*"

app: $(TARGET)
	@[ -d "package" ] || mkdir package
	@[ ! -d "package/yoMMD.app" ] || rm -r package/yoMMD.app
	@ mkdir -p package/yoMMD.app/Contents/MacOS
	@ mkdir package/yoMMD.app/Contents/Resources
	cp Info.plist package/yoMMD.app/Contents
	cp icons/yoMMD.icns package/yoMMD.app/Contents/Resources
	cp $(TARGET) package/yoMMD.app/Contents/MacOS


# Build bullet physics library
build-bullet: lib/bullet3/build/$(CMAKE_BUILDFILE)
	@cd lib/bullet3/build && cmake --build . -j && cmake --build . -t install

lib/bullet3/build/$(CMAKE_BUILDFILE):
	@[ -d "lib/bullet3/build" ] || mkdir lib/bullet3/build
	cd lib/bullet3/build && cmake \
		-DLIBRARY_OUTPUT_PATH=./           \
		-DBUILD_BULLET2_DEMOS=OFF          \
		-DBUILD_BULLET3=OFF                \
		-DBUILD_CLSOCKET=OFF               \
		-DBUILD_CPU_DEMOS=OFF              \
		-DBUILD_ENET=OFF                   \
		-DBUILD_EXTRAS=OFF                 \
		-DBUILD_OPENGL3_DEMOS=OFF          \
		-DBUILD_PYBULLET=OFF               \
		-DBUILD_SHARED_LIBS=OFF            \
		-DBUILD_UNIT_TESTS=OFF             \
		-DBULLET2_MULTITHREADING=OFF       \
		-DCMAKE_BUILD_TYPE=Release         \
		-DINSTALL_LIBS=ON                  \
		-DINSTALL_CMAKE_FILES=OFF          \
		-DUSE_DOUBLE_PRECISION=OFF         \
		-DUSE_GLUT=OFF                     \
		-DUSE_GRAPHICAL_BENCHMARK=OFF      \
		-DUSE_MSVC_INCREMENTAL_LINKING=OFF \
		-DUSE_MSVC_RUNTIME_LIBRARY_DLL=OFF \
		-DUSE_OPENVR=OFF                   \
		-DCMAKE_INSTALL_PREFIX=./          \
		$(CMAKE_GENERATOR)				   \
		..

# Build saba library
build-saba: lib/saba/build/$(CMAKE_BUILDFILE)
	cd lib/saba/build && cmake --build . -t Saba -j

lib/saba/build/$(CMAKE_BUILDFILE):
	@[ -d "lib/saba/build" ] || mkdir lib/saba/build
	cd lib/saba/build && cmake                  \
		-DCMAKE_BUILD_TYPE=RELEASE              \
		-DSABA_BULLET_ROOT=../../bullet3/build  \
		-DSABA_ENABLE_TEST=OFF                  \
		$(CMAKE_GENERATOR) ..

build-submodule:
	$(MAKE) build-bullet
	$(MAKE) build-saba

help:
	@echo "Available targets:"
	@echo "$(TARGET)               Build executable binary (The default target)"
	@echo "release             Release build (Only available on Windows)"
	@echo "debug               Debug build"
	@echo "run                 Build and run binary"
	@echo "clean               Clean build related files"
	@echo "app                 Make application bundle (Only available on macOS)"
	@echo "package             Make distribution package without any MMD models/motions"
	@echo "package-huge        Make distribution package with default config,"
	@echo "                        MMD model and motions included"
	@echo "build-bullet        Build bullet physics library"
	@echo "build-saba          Build saba library"
	@echo "bulid-submodule     Build submodule libraries"
	@echo "help                Show this help"

.PHONY: release debug help run clean package package-huge app
.PHONY: may-create-release-build
.PHONY: build-bullet build-saba update-sokol-shdc build-submodule
