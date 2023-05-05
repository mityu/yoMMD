CXX:=g++
CC:=gcc
TARGET:=yommd
OBJDIR:=./obj
SRC:=viewer.cpp config.cpp image.cpp util.cpp libs.mm
OBJ=$(addsuffix .o,$(addprefix $(OBJDIR)/,$(SRC)))
CFLAGS:=-O2 -Ilib/saba/src/ -Ilib/sokol -Ilib/glm -Ilib/stb -Ilib/toml11 -Wall -Wextra -pedantic
CPPFLAGS=-std=c++20
OBJCFLAGS=
LDFLAGS:=-Llib/saba/build/src -lSaba
LIB_SABA=
SOKOL_SHDC:=tool/sokol-shdc
SOKOL_SHDC_URL=

ifeq ($(OS),Windows_NT)
SRC+=main_windows.cpp
CFLAGS+=-I/mingw64/include/bullet
LDFLAGS+=-static -lkernel32 -luser32 -lshell32 -ld3d11 -ldxgi
LDFLAGS+=-LC:/msys64/mingw64/lib -lBulletCollision.dll -lBulletDynamics.dll -lBulletSoftBody.dll -lLinearMath.dll
LIB_SABA=lib/saba/build/src/libSaba.a
SOKOL_SHDC_URL:=https://github.com/floooh/sokol-tools-bin/raw/master/bin/win32/sokol-shdc.exe
SOKOL_SHDC:=$(SOKOL_SHDC).exe
else ifeq ($(shell uname),Darwin)
# TODO: Support intel mac?
CXX:=clang++
CC:=clang
SRC+=main_osx.mm
CFLAGS+=-I/opt/homebrew/include -I/opt/homebrew/include/bullet
LDFLAGS+=-framework Foundation -framework Cocoa -framework Metal -framework MetalKit -framework QuartzCore
OBJCFLAGS=-fobjc-arc
LDFLAGS+=-L/opt/homebrew/lib/bullet/single -lBulletCollision -lBulletDynamics -lBulletSoftBody -lLinearMath
# In order to prefer staic link library to dynamic link library with clang, we
# need to specify library with path, not with -l option.
# BULLET_LIB_PATH:=/opt/homebrew/lib/bullet/double/
# BULLET_LIBS:=libBulletCollision.a libBulletDynamics.a libBulletSoftBody.a libLinearMath.a
# LDFLAGS+=$(addprefix $(BULLET_LIB_PATH),$(BULLET_LIBS))
LIB_SABA=lib/saba/build/src/libSaba.a
SOKOL_SHDC_URL:=https://github.com/floooh/sokol-tools-bin/raw/master/bin/osx_arm64/sokol-shdc
endif

ifeq ($(strip $(LIB_SABA)),)
$(error LIB_SABA is not set)
endif

$(TARGET): $(OBJDIR) yommd.glsl.h $(LIB_SABA) $(OBJ)
	$(CXX) -o $@ $(OBJ) $(LDFLAGS)

debug: CFLAGS+=-g -O0
debug: OBJDIR:=$(OBJDIR)/debug
debug: TARGET:=$(TARGET)-debug
debug:
	$(MAKE) CFLAGS="$(CFLAGS)" OBJDIR="$(OBJDIR)" TARGET="$(TARGET)"

$(OBJDIR)/viewer.cpp.o: viewer.cpp yommd.glsl.h yommd.hpp
	$(CXX) -o $@ $(CPPFLAGS) $(CFLAGS) -c $<

$(OBJDIR)/%.cpp.o: %.cpp yommd.hpp
	$(CXX) -o $@ $(CPPFLAGS) $(CFLAGS) -c $<

ifneq ($(shell uname),Darwin)
$(OBJDIR)/libs.mm.o: libs.mm platform.hpp
	$(CC) -o $@ $(CFLAGS) -c -x c $<
endif

$(OBJDIR)/%.m.o: %.m yommd.hpp
	$(CC) -o $@ $(OBJCFLAGS) $(CFLAGS) -c $<

$(OBJDIR)/%.mm.o: %.mm yommd.hpp
	$(CXX) -o $@ $(CPPFLAGS) $(OBJCFLAGS) $(CFLAGS) -c $<

yommd.glsl.h: yommd.glsl $(SOKOL_SHDC)
	$(SOKOL_SHDC) --input $< --output $@ --slang metal_macos:hlsl5

run: $(TARGET)
	./$(TARGET)

clean:
	$(RM) $(TARGET)-debug $(OBJDIR)/debug/*.o
	$(RM) $(OBJDIR)/*.o $(TARGET) yommd.glsl.h

all: clean $(TARGET);

$(OBJDIR) tool/:
	mkdir -p $@

# Make distribution package
dist:
	echo 'TODO: Make distribution package'

# Build saba library
LIB_SABA_BUILDER=make -j4
ifeq ($(OS),Windows_NT)
LIB_SABA_BUILDER=ninja
endif
$(LIB_SABA):
	@[ -d "lib/saba/build" ] || mkdir lib/saba/build
	@cd lib/saba/build && cmake -DCMAKE_BUILD_TYPE=RELEASE .. && $(LIB_SABA_BUILDER) Saba

$(SOKOL_SHDC): tool/
	curl -L -o $@ $(SOKOL_SHDC_URL)
	chmod u+x $@

update-sokol-shdc:
	$(RM) $(SOKOL_SHDC) && make $(SOKOL_SHDC)

init-submodule:
	git submodule update --init

.PHONY: debug help run clean dist update-sokol-shdc init-submodule
