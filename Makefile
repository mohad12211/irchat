PROJECTNAME=irchat
ifndef PROFILE
.PHONY: default all release debug clean run run_release run_debug
default all: release
release run_release: export PROFILE := Release
release run_release: export EXTRA_CFLAGS := -O2 -march=native
debug run_debug: export PROFILE := Debug
debug run_debug: export EXTRA_CFLAGS := -DDEBUG -Og -ggdb3

clean:
	rm -rf build
	rm -rf external/libui-ng/build

release debug:
	@$(MAKE)

run_debug run_release:
	@$(MAKE) run

else
CC=clang
SRCDIR=src
OBJDIR=build/$(PROFILE)/obj
LIBDIR=libs
LIBOBJDIR=build/$(PROFILE)/libobj
DEPDIR=build/$(PROFILE)/dep
BINDIR=build/$(PROFILE)/bin
SRCS=$(wildcard $(SRCDIR)/*.c)
OBJS=$(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SRCS))
LIBS=$(wildcard $(LIBDIR)/*.c)
LIBSOBJS=$(patsubst $(LIBDIR)/%.c, $(LIBOBJDIR)/%.o, $(LIBS))
DEPS=$(patsubst $(SRCDIR)/%.c, $(DEPDIR)/%.d, $(SRCS))
BIN=$(BINDIR)/$(PROJECTNAME)

LIBUI_PATH=external/libui-ng
LIBUI_BUILD_PATH=$(LIBUI_PATH)/build/meson-out
LIBUI_LIB=$(LIBUI_BUILD_PATH)/libui.a

CFLAGS= -std=c23 -Wpedantic -Wextra -Wall -Wshadow-all -Wpointer-arith -Wcast-qual \
        -Wstrict-prototypes -Wmissing-prototypes -Wfloat-equal -Wswitch-enum \
        -Wmissing-declarations -I$(LIBUI_PATH)

DEPFLAGS=-MT $@ -MMD -MP -MF $(DEPDIR)/$*.d
LDFLAGS= -lm -L$(LIBUI_BUILD_PATH) -lui `pkg-config --cflags --libs gtk+-3.0`

PREFIX=/usr

$(BIN): $(LIBUI_LIB) $(OBJS) $(LIBSOBJS) | $(BINDIR)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(OBJS) $(LIBSOBJS) -o $@ $(LDFLAGS)

$(LIBUI_LIB):
	cd $(LIBUI_PATH) && meson setup build --buildtype=release --default-library=static
	ninja -C $(LIBUI_PATH)/build

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR) $(DEPDIR)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(DEPFLAGS) -c $< -o $@

$(LIBOBJDIR)/%.o: $(LIBDIR)/%.c | $(LIBOBJDIR)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c $< -o $@

$(OBJDIR) $(LIBOBJDIR) $(BINDIR) $(DEPDIR):
	@mkdir -p $@

run: $(BIN)
	$(BIN)

$(DEPS):
include $(wildcard $(DEPS))
endif
