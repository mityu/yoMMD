CXX:=g++
CC:=gcc
TARGET:=yoMMD
OBJDIR:=obj
SRCS:=viewer.cpp config.cpp resources.cpp image.cpp keyboard.cpp util.cpp libs.mm
CFLAGS:=-Ilib/saba/src/ -Ilib/sokol -Ilib/glm -Ilib/stb \
		-Ilib/toml11/include -Ilib/incbin -Ilib/bullet3/build/include/bullet \
		-Wall -Wextra -pedantic -MMD -MP \
		-Wno-missing-field-initializers
CPPFLAGS:=-std=c++20
OBJCFLAGS:=
LDFLAGS:=-Llib/saba/build/src -lSaba -Llib/bullet3/build/lib \
		 -lBulletDynamics -lBulletCollision -lBulletSoftBody -lLinearMath
PKGNAME_PLATFORM:=
CMAKE_GENERATOR:=
CMAKE_BUILDFILE:=Makefile

ifeq ($(OS),Windows_NT)
TARGET:=$(TARGET).exe
SRCS+=main_windows.cpp resource_windows.rc
LDFLAGS+=-static -lkernel32 -luser32 -lshell32 -ld3d11 -ldxgi -ldcomp -lgdi32 -ldwmapi -municode
SOKOL_SHDC:=lib/sokol-tools-bin/bin/win32/sokol-shdc.exe
PKGNAME_PLATFORM:=win-x86_64
CMAKE_GENERATOR:=-G "MSYS Makefiles"
else ifeq ($(shell uname),Darwin)
CXX:=clang++
CC:=clang
SRCS+=main_osx.mm
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

.PHONY: debug
debug: ./$(TARGET)

.PHONY: release
release: release/$(TARGET)

GENOBJS=$(SRCS:%=$1/%.o) $1/version.cpp.o
GENDEPS=$(patsubst %.o,%.d,$(call GENOBJS,$1))

define MKDIR
	@test -d "$1" || mkdir "$1"
endef

define GEN_BUILD_RULES
-include $(call GENDEPS,$(BUILDDIR)/$(OBJDIR))

$(BUILDDIR)/$(TARGET): $(BUILDDIR)/$(OBJDIR) $$(call GENOBJS,$(BUILDDIR)/$(OBJDIR))
	$$(CXX) -o $$@ $$(call GENOBJS,$(BUILDDIR)/$(OBJDIR)) $$(LDFLAGS) $(LDFLAGS_$1)

ifneq ($(shell uname),Darwin)
# When not on macOS, compile libs.mm as C program.
$(BUILDDIR)/$(OBJDIR)/libs.mm.o: libs.mm
	$$(CC) -o $$@ $$(CFLAGS) -c -x c $$<
endif

$(BUILDDIR)/$(OBJDIR)/version.cpp.o: auto/version.cpp
	$$(CXX) -o $$@ $$(CPPFLAGS) $$(CFLAGS) $(CFLAGS_$1) -c $$<

$(BUILDDIR)/$(OBJDIR)/viewer.cpp.o: auto/yommd.glsl.h auto/quad.glsl.h
$(BUILDDIR)/$(OBJDIR)/%.cpp.o: %.cpp
	$$(CXX) -o $$@ $$(CPPFLAGS) $$(CFLAGS) $(CFLAGS_$1) -c $$<

$(BUILDDIR)/$(OBJDIR)/%.mm.o: %.mm
	$$(CXX) -o $$@ $$(CPPFLAGS) $$(OBJCFLAGS) $$(CFLAGS) $(CFLAGS_$1) -c $$<

$(BUILDDIR)/$(OBJDIR)/resource_windows.rc.o: resource_windows.rc DpiAwareness.manifest
	windres -o $$@ $$<

$(BUILDDIR)/$(OBJDIR):
	mkdir -p $$@

.PHONY: clean-$1
clean-$1:
	$$(RM) $(BUILDDIR)/$(OBJDIR)/*.{o,d} $(BUILDDIR)/$(TARGET)
endef

BUILDDIR:=.
CFLAGS_debug:=-g -O0
$(eval $(call GEN_BUILD_RULES,debug))

BUILDDIR:=release
CFLAGS_release:=-O2
ifeq ($(OS),Windows_NT)
LDFLAGS_release:=-mwindows
endif
$(eval $(call GEN_BUILD_RULES,release))

auto/%.glsl.h: %.glsl $(SOKOL_SHDC) | auto/
	$(SOKOL_SHDC) --input $< --output $@ --slang metal_macos:hlsl5
ifeq ($(OS),Windows_NT)
	# CRLF -> LF
	tr -d \\r < $@ > $@.tmp && mv $@.tmp $@
endif

.PHONY: FORCE-EXECUTE
auto/version.cpp: FORCE-EXECUTE
	./scripts/gen-version-cpp

.PHONY: run
run: $(TARGET)
	./$(TARGET)

.PHONY: clean
clean: clean-debug clean-release
	$(RM) auto/*

.PHONY: all
all: clean $(TARGET);

auto/:
	mkdir -p $@

# Generate clang-format related subcommand.
define GEN_CLANG_FMT
.PHONY: $1
$1:
	clang-format $2 $(wildcard *.cpp *.hpp *.mm)
endef

$(eval $(call GEN_CLANG_FMT,fmt,-i))
$(eval $(call GEN_CLANG_FMT,fmt-check,--dry-run --Werror))

# Make distribution package
PKGNAME:=yoMMD-$(PKGNAME_PLATFORM)-$(shell date '+%Y%m%d%H%M').zip
.PHONY: package
package: release/$(TARGET)
	$(call MKDIR,package)
	zip package/$(PKGNAME) release/$(TARGET)

.PHONY: package-huge
package-huge: package
	@[ -d "default-attachments" ] && cd default-attachments && \
		zip -ur ../package/$(PKGNAME) \
		$(notdir $(wildcard default-attachments/*)) -x "*/.*"

.PHONY: app
app: release/$(TARGET)
	$(call MKDIR,package)
	@[ ! -d "package/yoMMD.app" ] || rm -r package/yoMMD.app
	@ mkdir -p package/yoMMD.app/Contents/MacOS
	@ mkdir package/yoMMD.app/Contents/Resources
	cp Info.plist package/yoMMD.app/Contents
	cp icons/yoMMD.icns package/yoMMD.app/Contents/Resources
	cp release/$(TARGET) package/yoMMD.app/Contents/MacOS


# Build bullet physics library
.PHONY: build-bullet
build-bullet: lib/bullet3/build/$(CMAKE_BUILDFILE)
	@cd lib/bullet3/build && cmake --build . -j && cmake --build . -t install

clean-bullet:
	$(RM) -r lib/bullet3/build

lib/bullet3/build/$(CMAKE_BUILDFILE):
	$(call MKDIR,lib/bullet3/build)
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
.PHONY: build-saba
build-saba: lib/saba/build/$(CMAKE_BUILDFILE)
	cd lib/saba/build && cmake --build . -t Saba -j

clean-saba:
	$(RM) -r lib/saba/build

lib/saba/build/$(CMAKE_BUILDFILE):
	$(call MKDIR,lib/saba/build)
	cd lib/saba/build && cmake                  \
		-DCMAKE_BUILD_TYPE=RELEASE              \
		-DSABA_BULLET_ROOT=../../bullet3/build  \
		-DSABA_ENABLE_TEST=OFF                  \
		$(CMAKE_GENERATOR) ..

.PHONY: build-submodule
build-submodule:
	$(MAKE) build-bullet
	$(MAKE) build-saba

.PHONY: init-submodule
init-submodule:
	git submodule update --init --recursive

.PHONY: update-submodule
update-submodule:
	git submodule update

.PHONY: help
help:
	@echo "Available targets:"
	@echo "debug               Debug build (The default target)"
	@echo "release             Release build"
	@echo "run                 Build debug binary and run it"
	@echo "clean               Clean build related files"
	@echo "fmt                 Format source code by clang-format"
	@echo "fmt-check           Check if source code is formatted"
	@echo "app                 Make application bundle (Only available on macOS)"
	@echo "package             Make distribution package without any MMD models/motions"
	@echo "package-huge        Make distribution package with default config,"
	@echo "                        MMD model and motions included"
	@echo "build-bullet        Build bullet physics library"
	@echo "build-saba          Build saba library"
	@echo "bulid-submodule     Build submodule libraries"
	@echo "help                Show this help"
