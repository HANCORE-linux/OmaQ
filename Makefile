# OmaQ — see docs/PLAN.md.

CC       ?= gcc
CFLAGS   ?= -std=c11 -Wall -Werror -O1
SANFLAGS ?= -fsanitize=address,undefined
PKG_CONFIG ?= pkg-config

TOX_OK := $(shell $(PKG_CONFIG) --exists libtoxcore && echo yes || \
	($(PKG_CONFIG) --exists toxcore && echo yes || echo no))
SIG_OK := $(shell $(PKG_CONFIG) --exists libsignal-protocol-c && echo yes || echo no)

ifeq ($(TOX_OK),yes)
  TOX_PC := $(shell $(PKG_CONFIG) --exists libtoxcore && echo libtoxcore || echo toxcore)
  CFLAGS += -DHAVE_TOX
  CFLAGS += $(shell $(PKG_CONFIG) --cflags $(TOX_PC))
  TOX_LIBS := $(shell $(PKG_CONFIG) --libs $(TOX_PC))
endif

ifeq ($(SIG_OK),yes)
  CFLAGS += -DHAVE_SIGNAL
  CFLAGS += $(shell $(PKG_CONFIG) --cflags libsignal-protocol-c)
  TOX_LIBS += $(shell $(PKG_CONFIG) --libs libsignal-protocol-c)
  TOX_LIBS += $(shell $(PKG_CONFIG) --libs libcrypto)
endif

LIB_SRC := helper/invite.c helper/roles.c helper/conversation.c \
	helper/json_io.c helper/line_reader.c helper/stdout_spool.c helper/store.c helper/message.c \
	helper/identity.c helper/tox_adapt.c helper/rate.c \
	helper/safety.c helper/qr.c helper/group.c helper/group_invite.c \
	helper/surface.c helper/file.c helper/avatar.c helper/av.c \
	helper/presence.c helper/receipt.c helper/message_action.c helper/ratchet.c helper/ratchet_pin.c helper/ratchet_adapt.c
HELPER_SRC := $(LIB_SRC) helper/omaq.c
TEST_SRC := tests/omaq_test.c helper/invite.c helper/roles.c helper/conversation.c \
	helper/json_io.c helper/line_reader.c helper/store.c helper/message.c helper/identity.c \
	helper/rate.c helper/safety.c helper/qr.c helper/group.c helper/group_invite.c \
	helper/surface.c helper/file.c helper/avatar.c helper/presence.c helper/receipt.c helper/message_action.c helper/ratchet.c \
	helper/ratchet_pin.c

BIN_TEST := tests/omaq_test
BIN_SPOOL_TEST := tests/stdout_spool_test
BIN_FILE_TRANSFER_TEST := tests/file_transfer_test
BIN_IPC_TEST_HELPER := tests/omaq_ipc_test_helper
BIN_HELP := helper/omaq

.PHONY: all test helper check-signal arch verify verify-0 verify-1 verify-1-offline verify-1-tox \
	verify-2 verify-3 verify-4 verify-5 verify-6 verify-7 verify-8 clean

all: $(BIN_TEST) helper

$(BIN_TEST): $(TEST_SRC)
	$(CC) -std=c11 -Wall -Werror -O1 $(SANFLAGS) -o $@ $(TEST_SRC)

$(BIN_SPOOL_TEST): tests/stdout_spool_test.c helper/stdout_spool.c helper/stdout_spool.h
	$(CC) -std=c11 -Wall -Werror -O1 $(SANFLAGS) -DOMAQ_STDOUT_SPOOL_MAX=5242880u \
		-o $@ tests/stdout_spool_test.c helper/stdout_spool.c

$(BIN_FILE_TRANSFER_TEST): tests/file_transfer_test.c helper/file.c helper/file.h helper/avatar.c helper/avatar.h helper/tox_adapt.h
	$(CC) -std=c11 -Wall -Werror -O1 $(SANFLAGS) -DHAVE_TOX -o $@ \
		tests/file_transfer_test.c helper/file.c helper/avatar.c

$(BIN_IPC_TEST_HELPER): $(HELPER_SRC)
	$(CC) -std=c11 -Wall -Werror -Wno-unused-function -O1 $(SANFLAGS) -DOMAQ_IPC_TEST \
		-DOMAQ_STDOUT_SPOOL_MAX=5242880u -o $@ $(HELPER_SRC)

check-signal:
	@if [ "$(SIG_OK)" != "yes" ]; then \
		echo "omaq: libsignal-protocol-c is required for direct-message encryption" >&2; \
		echo "omaq: install it before running 'make helper'" >&2; \
		exit 1; \
	fi

$(BIN_HELP): check-signal $(HELPER_SRC)
	$(CC) $(CFLAGS) -o $@ $(HELPER_SRC) $(TOX_LIBS)

test: $(BIN_TEST) $(BIN_SPOOL_TEST) $(BIN_FILE_TRANSFER_TEST) $(BIN_IPC_TEST_HELPER)
	./$(BIN_TEST)
	./$(BIN_SPOOL_TEST)
	./$(BIN_FILE_TRANSFER_TEST)
	python3 tests/ipc-regression.py ./$(BIN_IPC_TEST_HELPER)

helper: $(BIN_HELP)

arch:
	sh scripts/arch-check.sh

PHASE ?= $(shell cat .phase 2>/dev/null || echo 0)

verify:
	$(MAKE) verify-$(PHASE)

verify-0: test arch
	sh tests/no-signal-build.sh
	bash -n packaging/PKGBUILD
	test -f LICENSE.MIT
	test -f LICENSE.GPL-3
	test -f THIRD_PARTY.md
	@echo "verify-0: ok"

verify-1-offline: test arch helper
	sh tests/lock-elect.sh
	sh tests/two-clients.sh
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

verify-2: test arch helper
	@if [ "$(TOX_OK)" != "yes" ]; then \
		echo "verify-2: toxcore not installed. Run: omarchy pkg add toxcore" >&2; \
		exit 1; \
	fi
	sh tests/lock-elect.sh
	sh tests/two-clients.sh
	omarchy plugin validate .
	sh tests/phase2.sh
	@echo "verify-2: ok"

verify-3: test arch helper
	@if [ "$(TOX_OK)" != "yes" ]; then \
		echo "verify-3: toxcore not installed" >&2; \
		exit 1; \
	fi
	test -f docs/stages/03-toxcore.md
	sh tests/lock-elect.sh
	omarchy plugin validate .
	sh tests/phase3.sh
	@echo "verify-3: ok"

verify-4: test arch helper
	test -f themes/paper.json
	test -f themes/ink.json
	test -f themes/moss.json
	test -f themes/dusk.json
	test -f themes/ember.json
	test -f themes/system.json
	test -f sounds/click.wav
	test -f sounds/pop.wav
	test -f sounds/bell.wav
	test -f sounds/soft.wav
	test -f sounds/knock.wav
	sh tests/lock-elect.sh
	omarchy plugin validate .
	sh tests/phase4.sh
	@echo "verify-4: ok"

verify-5: test arch helper
	@if [ "$(TOX_OK)" != "yes" ]; then \
		echo "verify-5: toxcore not installed" >&2; \
		exit 1; \
	fi
	sh tests/lock-elect.sh
	omarchy plugin validate .
	sh tests/phase5.sh
	@echo "verify-5: ok"

verify-6: test arch helper
	@if [ "$(TOX_OK)" != "yes" ]; then \
		echo "verify-6: toxcore not installed" >&2; \
		exit 1; \
	fi
	sh tests/lock-elect.sh
	omarchy plugin validate .
	sh tests/phase6.sh
	sh tests/encryptsave.sh
	@echo "verify-6: ok"

verify-7:
	@echo "$@: halted (AUR registration off)" >&2; exit 1

verify-8: test arch helper
	@if [ "$(TOX_OK)" != "yes" ]; then echo "verify-8: toxcore missing" >&2; exit 1; fi
	@if [ "$(SIG_OK)" != "yes" ]; then \
		echo "verify-8: libsignal-protocol-c missing. omarchy pkg add libsignal-protocol-c" >&2; \
		exit 1; \
	fi
	test -f docs/stages/08-ratchet.md
	sh tests/lock-elect.sh
	omarchy plugin validate .
	sh tests/phase8.sh
	sh tests/ratchet-restart.sh
	@echo "verify-8: ok"

clean:
	rm -f $(BIN_TEST) $(BIN_SPOOL_TEST) $(BIN_FILE_TRANSFER_TEST) \
		$(BIN_IPC_TEST_HELPER) $(BIN_HELP)
