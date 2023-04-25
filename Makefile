CXX:=g++
CC:=gcc
TARGET:=yommd
OBJDIR:=./obj
SRC:=viewer.cpp image.cpp libs.mm
OBJ=$(addsuffix .o,$(addprefix $(OBJDIR)/,$(SRC)))
CFLAGS:=-Ilib/saba/src/ -Ilib/sokol -Ilib/glm -Ilib/stb -Wall -Wextra -pedantic
CPPFLAGS=-std=c++17
OBJCFLAGS=-fobjc-arc
LDFLAGS:=-Llib/saba/build/src -lSaba
LIB_SABA=
SOKOL_SHDC:=tool/sokol-shdc
SOKOL_SHDC_URL=

ifeq ($(OS),Windows_NT)
# TODO:
CFLAGS+=-mwin32
LDFLAGS+=-static -lkernel32 -luser32 -lshell32 -ld3d11 -ldxgi
SOKOL_SHDC_URL:=https://github.com/floooh/sokol-tools-bin/raw/master/bin/win32/sokol-shdc.exe
SOKOL_SHDC:=$(SOKOL_SHDC).exe
else ifeq ($(shell uname),Darwin)
# TODO: Support intel mac?
CXX:=clang++
CC:=clang
SRC+=main_osx.mm
CFLAGS+=-I/opt/homebrew/include
LDFLAGS+=-framework Foundation -framework Cocoa -framework Metal -framework MetalKit -framework QuartzCore
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
	$(CXX) -o $@ $(LDFLAGS) $(OBJ)
	@echo 'TODO: Set sample value'

debug: CFLAGS+=-g -O0
debug: clean $(TARGET)

$(OBJDIR)/viewer.cpp.o: viewer.cpp yommd.glsl.h yommd.hpp
	$(CXX) -o $@ $(CPPFLAGS) $(CFLAGS) -c $<

$(OBJDIR)/%.cpp.o: %.cpp yommd.hpp
	$(CXX) -o $@ $(CPPFLAGS) $(CFLAGS) -c $<

$(OBJDIR)/%.m.o: %.m yommd.hpp
	$(CC) -o $@ $(OBJCFLAGS) $(CFLAGS) -c $<

$(OBJDIR)/%.mm.o: %.mm yommd.hpp
	$(CXX) -o $@ $(CPPFLAGS) $(OBJCFLAGS) $(CFLAGS) -c $<

yommd.glsl.h: yommd.glsl tool/sokol-shdc
	tool/sokol-shdc --input $< --output $@ --slang glsl330:metal_macos:hlsl5

run: $(TARGET)
	./$(TARGET)

clean:
	$(RM) $(OBJDIR)/* $(TARGET) yommd.glsl.h

$(OBJDIR) tool/:
	mkdir -p $@

# Make distribution package
dist:
	echo 'TODO: Make distribution package'

# Build saba library
$(LIB_SABA):
	@[ -d "lib/saba/build" ] || mkdir lib/saba/build
	@cd lib/saba/build && cmake -DCMAKE_BUILD_TYPE=RELEASE .. && make -j4 Saba

$(SOKOL_SHDC): tool/
	curl -L -o $@ $(SOKOL_SHDC_URL)
	chmod u+x $@

update-sokol-shdc:
	$(RM) $(SOKOL_SHDC) && make $(SOKOL_SHDC)

.PHONY: help run clean dist update-sokol-shdc
