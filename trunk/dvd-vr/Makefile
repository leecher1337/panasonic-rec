NAME := dvd-vr
VERSION := 0.9.8b
PREFIX := /usr/local
DESTDIR :=

# Using override to append to user supplied CFLAGS
override CFLAGS+=-std=gnu99 -Wall -Wextra -Wpadded -DVERSION='"$(VERSION)"'

# Use `make DEBUG=1` to build debugging version
ifeq ($(DEBUG),1)
    override CFLAGS+=-ggdb
else
    override CFLAGS+=-O3 -DNDEBUG
endif

# Use `make UNIVERSAL=1` to build a Mac OS X universal binary
HAVE_MACOSX := $(shell gcc -xc -mmacosx-version-min -c - < /dev/null 2>/dev/null && echo 1 || echo 0)
ifeq ($(UNIVERSAL),1)
ifeq ($(HAVE_MACOSX),1)
    UNIVERSAL_BINARY := -mmacosx-version-min=10.4 -force_cpusubtype_ALL -arch x86_64 -arch i386 -arch ppc
    override CFLAGS+=$(UNIVERSAL_BINARY)
    override LDFLAGS+=$(UNIVERSAL_BINARY)
else
    $(warning "Warning: Universal binaries are only supported on Mac OS X")
endif
endif

# Use iconv when available
# This is not cached across make invocations unfortunately
HAVE_ICONV := $(shell echo "\#include <iconv.h>" | cpp >/dev/null 2>&1 && echo 1 || echo 0)
ifeq ($(HAVE_ICONV),1)
    override CFLAGS+=-DHAVE_ICONV

    # Work around const warnings
    ICONV_CONST := $(shell (echo "\#include <iconv.h>"; echo "size_t iconv(iconv_t,char **,size_t*,char **,size_t*);") | $(CC) -xc -S - -o- >/dev/null 2>&1 || echo const)
    override CFLAGS+=-DICONV_CONST="$(ICONV_CONST)"

    # Add -liconv where available/required like Mac OS X & CYGWIN for example
    NEED_LICONV := $(shell echo "int main(void){}" | $(CC) -xc -liconv - -o liconv_test 2>/dev/null && echo 1 || echo 0; rm -f liconv_test)
    ifeq ($(NEED_LICONV),1)
        LDFLAGS+=-liconv
    endif
else
    $(warning "Warning: title translation support disabled as libiconv not installed")
endif

# Strip debugging symbols if not debugging
ifneq ($(DEBUG),1)
    LDFLAGS+=-Wl,-S
endif

HOST := $(shell uname | tr '[:lower:]' '[:upper:]')
ifneq (,$(findstring CYGWIN,$(HOST)))
    EXEEXT := .exe
endif

BINARY := $(NAME)$(EXEEXT)
SOURCES := *.c
OBJECTS := $(patsubst %.c,%.o,$(wildcard $(SOURCES)))

#first target is the default
$(BINARY): $(OBJECTS)
	$(CC) $(LIBS) $(OBJECTS) $(LDFLAGS) -o $@

#Enhance implicit rule for .c -> .o to depend on
#this Makefile itself so that a recompile is done
#if hardcoded settings like version etc. change.
%.o: %.c Makefile
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

.PHONY: all
all: $(BINARY) man

.PHONY: dist
dist: man clean
	mkdir $(NAME)-$(VERSION)
	tar --exclude $(NAME)-$(VERSION) --exclude .svn --exclude .git -c . | (cd $(NAME)-$(VERSION) && tar -xp)
	tar c $(NAME)-$(VERSION) | gzip -9 > $(NAME)-$(VERSION).tar.gz
	-@rm -Rf $(NAME)-$(VERSION)

.PHONY: clean
clean:
	-@rm -f *.o $(BINARY) core*
	-@rm -Rf $(NAME)-$(VERSION)*

man/$(NAME).1: $(BINARY) man/$(NAME).x
	help2man --no-info --include=man/$(NAME).x ./$(BINARY) > man/$(NAME).1

.PHONY: man
man: man/$(NAME).1

datadir := $(PREFIX)/share
mandir := $(datadir)/man
man1dir := $(mandir)/man1
bindir := $(PREFIX)/bin

.PHONY: install
install: $(BINARY)
	-@mkdir -p $(DESTDIR)$(bindir)
	cp -p $(BINARY) $(DESTDIR)$(bindir)
	-@mkdir -p $(DESTDIR)$(man1dir)
	gzip -c man/$(NAME).1 > $(DESTDIR)$(man1dir)/$(NAME).1.gz

.PHONY:	uninstall
uninstall:
	rm $(DESTDIR)$(bindir)/$(BINARY)
	rm $(DESTDIR)$(man1dir)/$(NAME).1*
