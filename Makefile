
# Based on https://stackoverflow.com/a/32982136

SRC_DIR := src
OBJ_DIR := build

# use either /usr/local or ~/.local
# other directories will require modifying hardcoded module search paths
INSTALL_DIR ?= /usr/local

CC := clang
CFLAGS := -g -Werror -O2 -flto
LDFLAGS := -rdynamic -O2 -flto
CPPFLAGS := -MMD -MP
LDLIBS := -lm -ldl -lgc -lreadline

SOURCES := $(wildcard $(SRC_DIR)/*.c)
OBJECTS := $(SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
DEPS := $(wildcard $(OBJ_DIR)/*.d)

.PHONY: clean all install

all: fp modules/modpcre2.so

install:
	install fp $(INSTALL_DIR)/bin
	install -d $(INSTALL_DIR)/lib/fruity
	install modules/* $(INSTALL_DIR)/lib/fruity

fp: $(OBJECTS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(OBJECTS): $(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $(OUTPUT_OPTION) $<

# modules/modbuiltin.so: src/modules/builtin.c
# 	$(CC) $(CFLAGS) -shared -o modules/modbuiltin.so -fPIC src/modules/builtin.c

modules/modpcre2.so: src/modules/modpcre2.c
	$(CC) $(CFLAGS) -shared -o modules/modpcre2.so -fPIC src/modules/modpcre2.c -lpcre2-8

clean:
	$(RM) $(DEPS) $(OBJECTS) modules/mod*.so

include $(DEPS)
