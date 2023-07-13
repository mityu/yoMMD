CXX:=g++
CC:=gcc
TARGET:=yoMMD
TARGET_DEBUG:=yoMMD-debug
OBJDIR:=./obj
SRC:=viewer.cpp config.cpp resources.cpp image.cpp util.cpp libs.mm
OBJ=$(addsuffix .o,$(addprefix $(OBJDIR)/,$(SRC)))
CFLAGS:=-O2 -Ilib/saba/src/ -Ilib/sokol -Ilib/glm -Ilib/stb \
		-Ilib/toml11 -Ilib/incbin -Ilib/bullet3/build/include/bullet \
		-Wall -Wextra -pedantic
CPPFLAGS=-std=c++20
OBJCFLAGS=
LDFLAGS:=-Llib/saba/build/src -lSaba -Llib/bullet3/build/lib \
		 -lBulletDynamics -lBulletCollision -lBulletSoftBody -lLinearMath
SOKOL_SHDC:=tool/sokol-shdc
SOKOL_SHDC_URL=
PKGNAME_PLATFORM:=

ifeq ($(OS),Windows_NT)
TARGET:=$(TARGET).exe
TARGET_DEBUG:=$(TARGET_DEBUG).exe
SRC+=main_windows.cpp appicon_windows.rc
CFLAGS+=-I/mingw64/include/bullet -Wno-missing-field-initializers
LDFLAGS+=-static -lkernel32 -luser32 -lshell32 -ld3d11 -ldxgi -ldcomp -lgdi32
SOKOL_SHDC_URL:=https://github.com/floooh/sokol-tools-bin/raw/master/bin/win32/sokol-shdc.exe
SOKOL_SHDC:=$(SOKOL_SHDC).exe
PKGNAME_PLATFORM:=win-x86_64
else ifeq ($(shell uname),Darwin)
# TODO: Support intel mac?
CXX:=clang++
CC:=clang
SRC+=main_osx.mm
CFLAGS+=-I/opt/homebrew/include
LDFLAGS+=-framework Foundation -framework Cocoa -framework Metal -framework MetalKit -framework QuartzCore
OBJCFLAGS=-fobjc-arc
SOKOL_SHDC_URL:=https://github.com/floooh/sokol-tools-bin/raw/master/bin/osx_arm64/sokol-shdc
PKGNAME_PLATFORM:=darwin-arm64
endif

$(TARGET): $(OBJDIR) yommd.glsl.h $(OBJ)
	$(CXX) -o $@ $(OBJ) $(LDFLAGS)

ifeq ($(OS),Windows_NT)
release:
	@[ -f "$(TARGET)" ] && rm $(TARGET)
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

$(OBJDIR)/viewer.cpp.o: viewer.cpp yommd.glsl.h yommd.hpp
	$(CXX) -o $@ $(CPPFLAGS) $(CFLAGS) -c $<

$(OBJDIR)/%.cpp.o: %.cpp yommd.hpp
	$(CXX) -o $@ $(CPPFLAGS) $(CFLAGS) -c $<

ifneq ($(shell uname),Darwin)
# When not on macOS, compile libs.mm as C program.
$(OBJDIR)/libs.mm.o: libs.mm platform.hpp
	$(CC) -o $@ $(CFLAGS) -c -x c $<
endif

$(OBJDIR)/%.m.o: %.m yommd.hpp
	$(CC) -o $@ $(OBJCFLAGS) $(CFLAGS) -c $<

$(OBJDIR)/%.mm.o: %.mm yommd.hpp
	$(CXX) -o $@ $(CPPFLAGS) $(OBJCFLAGS) $(CFLAGS) -c $<

$(OBJDIR)/%.rc.o: %.rc
	windres -o $@ $^

yommd.glsl.h: yommd.glsl $(SOKOL_SHDC)
	$(SOKOL_SHDC) --input $< --output $@ --slang metal_macos:hlsl5
ifeq ($(OS),Windows_NT)
	# CRLF -> LF
	@# TODO: Better way?
	vim -u NONE -i NONE -N -n -e -s -c "set fileformat=unix | write | quitall!" $@
endif

run: $(TARGET)
	./$(TARGET)

clean:
	$(RM) $(TARGET)-debug $(OBJDIR)/debug/*.o
	$(RM) $(OBJDIR)/*.o $(TARGET) yommd.glsl.h

all: clean $(TARGET);

$(OBJDIR) tool/:
	mkdir -p $@

# Make distribution package
PKGNAME:=yoMMD-$(PKGNAME_PLATFORM)-$(shell date '+%Y%m%d%H%M').zip
package-tiny: may-create-release-build
	@[ -d "package" ] || mkdir package
	zip package/$(PKGNAME) $(TARGET)

package: package-tiny
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
LIB_BULLET_BUILDER=make -j4 && make install
ifeq ($(OS),Windows_NT)
LIB_BULLET_BUILDER=ninja && ninja install
endif
build-bullet: lib/bullet3/build lib/bullet3/Makefile
	@cd lib/bullet3 && $(LIB_BULLET_BUILDER)

lib/bullet3/build:
	mkdir lib/bullet3/build

lib/bullet3/Makefile:
	cd lib/bullet3 && cmake \
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
		-DCMAKE_INSTALL_PREFIX=./build     \
		.

# Build saba library
LIB_SABA_BUILDER=make -j4 Saba
ifeq ($(OS),Windows_NT)
LIB_SABA_BUILDER=ninja Saba
endif
build-saba:
	@[ -d "lib/saba/build" ] || mkdir lib/saba/build
	@cd lib/saba/build && cmake -DCMAKE_BUILD_TYPE=RELEASE .. && $(LIB_SABA_BUILDER)

$(SOKOL_SHDC): tool/
	curl -L -o $@ $(SOKOL_SHDC_URL)
	chmod u+x $@

update-sokol-shdc:
	$(RM) $(SOKOL_SHDC) && make $(SOKOL_SHDC)

init-submodule:
	git submodule update --init
	$(MAKE) build-bullet
	$(MAKE) build-saba

help:
	@echo "Available targets:"
	@echo "$(TARGET)		Build executable binary (The default target)"
	@echo "release		Release build (Only available on Windows)"
	@echo "debug		Debug build"
	@echo "run		Build and run binary"
	@echo "clean		Clean build related files"
	@echo "package-tiny	Make distribution package without any MMD models/motions"
	@echo "package		Make distribution package with default config,"
	@echo "		MMD model and motions included"
	@echo "build-bullet	Build bullet physics library"
	@echo "build-saba	Build saba library"
	@echo "update-sokol-shdc	Update sokol-shdc tool"
	@echo "init-submodule	Init submodule, and build bullet and saba library"
	@echo "help		Show this help"

.PHONY: release debug help run clean package package-tiny app
.PHONY: may-create-release-build
.PHONY: build-bullet build-saba update-sokol-shdc init-submodule
