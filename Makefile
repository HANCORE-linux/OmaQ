# OmaQ — see docs/PLAN.md.

CC       ?= gcc
CFLAGS   ?= -std=c11 -Wall -Werror -O1
SANFLAGS ?= -fsanitize=address,undefined
PKG_CONFIG ?= pkg-config

TOX_OK := $(shell $(PKG_CONFIG) --exists libtoxcore && echo yes || \
	($(PKG_CONFIG) --exists toxcore && echo yes || echo no))

ifeq ($(TOX_OK),yes)
  TOX_PC := $(shell $(PKG_CONFIG) --exists libtoxcore && echo libtoxcore || echo toxcore)
  CFLAGS += -DHAVE_TOX
  CFLAGS += $(shell $(PKG_CONFIG) --cflags $(TOX_PC))
  TOX_LIBS := $(shell $(PKG_CONFIG) --libs $(TOX_PC))
endif

LIB_SRC := helper/invite.c helper/roles.c helper/conversation.c \
	helper/json_io.c helper/store.c helper/message.c \
	helper/identity.c helper/tox_adapt.c
HELPER_SRC := $(LIB_SRC) helper/omaq.c
TEST_SRC := tests/omaq_test.c helper/invite.c helper/roles.c helper/conversation.c \
	helper/json_io.c helper/store.c helper/message.c

BIN_TEST := tests/omaq_test
BIN_HELP := helper/omaq

.PHONY: all test helper arch verify verify-0 verify-1 verify-1-offline verify-1-tox \
	verify-2 verify-3 verify-4 verify-5 verify-6 verify-7 clean

all: $(BIN_TEST) $(BIN_HELP)

$(BIN_TEST): $(TEST_SRC)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ $(TEST_SRC)

$(BIN_HELP): $(HELPER_SRC)
	$(CC) $(CFLAGS) $(SANFLAGS) -o $@ $(HELPER_SRC) $(TOX_LIBS)

test: $(BIN_TEST)
	./$(BIN_TEST)

helper: $(BIN_HELP)

arch:
	sh scripts/arch-check.sh

PHASE ?= $(shell cat .phase 2>/dev/null || echo 0)

verify:
	$(MAKE) verify-$(PHASE)

verify-0: test arch
	bash -n packaging/PKGBUILD
	test -f LICENSE.MIT
	test -f LICENSE.GPL-3
	test -f THIRD_PARTY.md
	@echo "verify-0: ok"

verify-1-offline: test arch helper
	sh tests/lock-elect.sh
	omarchy plugin validate .
	@echo "verify-1-offline: ok"

verify-1-tox: helper
	@if [ "$(TOX_OK)" != "yes" ]; then \
		echo "verify-1-tox: toxcore not installed. Run: omarchy pkg add toxcore" >&2; \
		exit 1; \
	fi
	sh tests/two-homes.sh
	@echo "verify-1-tox: ok"

verify-1: verify-1-offline
	@if [ "$(TOX_OK)" = "yes" ]; then $(MAKE) verify-1-tox; \
	else echo "verify-1: offline ok; tox not enabled (omarchy pkg add toxcore)"; fi

verify-2 verify-3 verify-4 verify-5 verify-6 verify-7:
	@echo "$@: not this phase (current=$(PHASE))" >&2; exit 1

clean:
	rm -f $(BIN_TEST) $(BIN_HELP)
