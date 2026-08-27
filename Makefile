# OmaQ — see docs/PLAN.md.

CC       ?= gcc
CFLAGS   ?= -std=c11 -Wall -Werror -O2
SANFLAGS ?= -fsanitize=address,undefined
HARDEN_CFLAGS ?= -D_FORTIFY_SOURCE=3 -fstack-protector-strong -fstack-clash-protection -fPIE
HARDEN_LDFLAGS ?= -Wl,-z,relro,-z,now -pie
PKG_CONFIG ?= pkg-config

TOX_OK := $(shell $(PKG_CONFIG) --exists libtoxcore && echo yes || \
	($(PKG_CONFIG) --exists toxcore && echo yes || echo no))
SIG_OK := $(shell $(PKG_CONFIG) --exists libsignal-protocol-c && echo yes || echo no)
PULSE_OK := $(shell $(PKG_CONFIG) --exists libpulse && echo yes || echo no)
IMAGE_OK := $(shell $(PKG_CONFIG) --exists libpng libjpeg libwebp && echo yes || echo no)

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

ifeq ($(IMAGE_OK),yes)
  AVATAR_CFLAGS := -DHAVE_AVATAR_DECODERS $(shell $(PKG_CONFIG) --cflags libpng libjpeg libwebp)
  AVATAR_LIBS := $(shell $(PKG_CONFIG) --libs libpng libjpeg libwebp)
  CFLAGS += $(AVATAR_CFLAGS)
  TOX_LIBS += $(AVATAR_LIBS)
endif

ifeq ($(PULSE_OK),yes)
  CFLAGS += -DHAVE_PULSE
  CFLAGS += $(shell $(PKG_CONFIG) --cflags libpulse)
  TOX_LIBS += $(shell $(PKG_CONFIG) --libs libpulse)
endif

LIB_SRC := helper/invite.c helper/roles.c helper/conversation.c \
	helper/json_io.c helper/line_reader.c helper/stdout_spool.c helper/store.c helper/message.c \
	helper/identity.c helper/identity_guard.c helper/tox_adapt.c helper/rate.c \
	helper/safety.c helper/qr.c helper/group.c helper/group_invite.c \
	helper/surface.c helper/file.c helper/avatar.c helper/av.c \
	helper/presence.c helper/receipt.c helper/message_action.c helper/direct_state.c \
	helper/ratchet.c helper/ratchet_pin.c helper/ratchet_adapt.c
HELPER_SRC := $(LIB_SRC) helper/omaq.c
TEST_SRC := tests/omaq_test.c helper/invite.c helper/roles.c helper/conversation.c \
	helper/json_io.c helper/line_reader.c helper/store.c helper/message.c helper/identity.c \
	helper/identity_guard.c \
	helper/rate.c helper/safety.c helper/qr.c helper/group.c helper/group_invite.c \
	helper/surface.c helper/file.c helper/avatar.c helper/presence.c helper/receipt.c helper/message_action.c \
	helper/direct_state.c helper/ratchet.c helper/ratchet_pin.c

BIN_TEST := tests/omaq_test
BIN_SPOOL_TEST := tests/stdout_spool_test
BIN_FILE_TRANSFER_TEST := tests/file_transfer_test
BIN_AV_STATE_TEST := tests/av_state_test
BIN_RATCHET_PREKEY_TEST := tests/ratchet_prekey_test
BIN_IDENTITY_GUARD_TEST := tests/identity_guard_test
BIN_IPC_TEST_HELPER := tests/omaq_ipc_test_helper

ifeq ($(SIG_OK),yes)
  SIGNAL_TEST_TARGET := $(BIN_RATCHET_PREKEY_TEST)
endif
ifeq ($(TOX_OK),yes)
  IDENTITY_GUARD_TEST_TARGET := $(BIN_IDENTITY_GUARD_TEST)
endif
BIN_HELP := helper/omaq
ifeq ($(TOX_OK)$(SIG_OK)$(PULSE_OK)$(IMAGE_OK),yesyesyesyes)
  REINVITE_TEST_TARGET := $(BIN_HELP)
  REINVITE_TEST_COMMAND := sh tests/reinvite-recovery.sh
else
  REINVITE_TEST_COMMAND := @echo "reinvite-recovery: skipped (full helper dependencies unavailable)"
endif

.PHONY: all test helper check-signal check-audio check-images arch verify verify-0 verify-1 verify-1-offline verify-1-tox \
	verify-2 verify-3 verify-4 verify-5 verify-6 verify-7 verify-8 clean

all: $(BIN_TEST) helper

$(BIN_TEST): $(TEST_SRC)
	$(CC) -std=c11 -Wall -Werror -O1 $(SANFLAGS) $(AVATAR_CFLAGS) -o $@ $(TEST_SRC) $(AVATAR_LIBS)

$(BIN_SPOOL_TEST): tests/stdout_spool_test.c helper/stdout_spool.c helper/stdout_spool.h
	$(CC) -std=c11 -Wall -Werror -O1 $(SANFLAGS) -DOMAQ_STDOUT_SPOOL_MAX=5242880u \
		-o $@ tests/stdout_spool_test.c helper/stdout_spool.c

$(BIN_FILE_TRANSFER_TEST): tests/file_transfer_test.c helper/file.c helper/file.h helper/avatar.c helper/avatar.h helper/tox_adapt.h
	$(CC) -std=c11 -Wall -Werror -O1 $(SANFLAGS) -DHAVE_TOX $(AVATAR_CFLAGS) -o $@ \
		tests/file_transfer_test.c helper/file.c helper/avatar.c $(AVATAR_LIBS)

$(BIN_AV_STATE_TEST): tests/av_state_test.c helper/av.c helper/av.h helper/tox_adapt.h
	$(CC) -std=c11 -Wall -Werror -Wno-unused-function -O1 $(SANFLAGS) -DHAVE_TOX -o $@ \
		tests/av_state_test.c helper/av.c -pthread

$(BIN_RATCHET_PREKEY_TEST): tests/ratchet_prekey_test.c helper/ratchet.c helper/ratchet_adapt.c helper/ratchet.h
	$(CC) -std=c11 -Wall -Werror -O1 $(SANFLAGS) -DHAVE_SIGNAL \
		$(shell $(PKG_CONFIG) --cflags libsignal-protocol-c) -o $@ \
		tests/ratchet_prekey_test.c helper/ratchet.c helper/ratchet_adapt.c \
		$(shell $(PKG_CONFIG) --libs libsignal-protocol-c libcrypto)

$(BIN_IDENTITY_GUARD_TEST): tests/identity_guard_test.c helper/identity_guard.c helper/tox_adapt.c helper/file.c
	$(CC) -std=c11 -Wall -Werror -O1 $(SANFLAGS) -DHAVE_TOX -DOMAQ_TOX_TEST \
		-DOMAQ_IDENTITY_GUARD_TEST \
		$(shell $(PKG_CONFIG) --cflags $(TOX_PC)) -o $@ \
		tests/identity_guard_test.c helper/identity_guard.c helper/tox_adapt.c helper/file.c \
		$(shell $(PKG_CONFIG) --libs $(TOX_PC))

$(BIN_IPC_TEST_HELPER): $(HELPER_SRC)
	$(CC) -std=c11 -Wall -Werror -Wno-unused-function -O1 $(SANFLAGS) -DOMAQ_IPC_TEST \
		-DOMAQ_STDOUT_SPOOL_MAX=5242880u $(AVATAR_CFLAGS) -o $@ $(HELPER_SRC) \
		$(AVATAR_LIBS)

check-signal:
	@if [ "$(SIG_OK)" != "yes" ]; then \
		echo "omaq: libsignal-protocol-c is required for direct-message encryption" >&2; \
		echo "omaq: install it before running 'make helper'" >&2; \
		exit 1; \
	fi

check-audio:
	@if [ "$(PULSE_OK)" != "yes" ]; then \
		echo "omaq: libpulse is required for voice calls" >&2; \
		echo "omaq: install libpulse before running 'make helper'" >&2; \
		exit 1; \
	fi

check-images:
	@if [ "$(IMAGE_OK)" != "yes" ]; then \
		echo "omaq: libpng, libjpeg and libwebp are required for safe avatar decoding" >&2; \
		exit 1; \
	fi

$(BIN_HELP): check-signal check-audio check-images $(HELPER_SRC)
	$(CC) $(CFLAGS) $(HARDEN_CFLAGS) $(HARDEN_LDFLAGS) -o $@ $(HELPER_SRC) $(TOX_LIBS)

test: $(BIN_TEST) $(BIN_SPOOL_TEST) $(BIN_FILE_TRANSFER_TEST) $(BIN_AV_STATE_TEST) $(SIGNAL_TEST_TARGET) $(IDENTITY_GUARD_TEST_TARGET) $(BIN_IPC_TEST_HELPER) $(REINVITE_TEST_TARGET)
	./$(BIN_TEST)
	./$(BIN_SPOOL_TEST)
	./$(BIN_FILE_TRANSFER_TEST)
	./$(BIN_AV_STATE_TEST)
	@if [ "$(TOX_OK)" = "yes" ]; then ./$(BIN_IDENTITY_GUARD_TEST); fi
	@if [ "$(SIG_OK)" = "yes" ]; then ./$(BIN_RATCHET_PREKEY_TEST); fi
	sh tests/float-script.sh
	sh tests/nonblocking-invite.sh
	sh tests/input-mask.sh
	sh tests/surface-owner.sh
	sh tests/paste-image.sh
	sh tests/protocol-compat.sh
	$(REINVITE_TEST_COMMAND)
	sh tests/uninstall.sh
	python3 tests/ipc-regression.py ./$(BIN_IPC_TEST_HELPER)

helper: $(BIN_HELP)
	sh tests/helper-hardening.sh $(BIN_HELP)

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
	test -f sounds/qq.oga
	test -f sounds/wechat.oga
	test -f sounds/skype.oga
	test -f sounds/msn.oga
	test -f sounds/aurora.oga
	test -f sounds/crystal.oga
	test -f sounds/ripple.oga
	test -f sounds/glow.oga
	test -f sounds/halo.oga
	test -f sounds/phone.oga
	test -f sounds/ATTRIBUTION.md
	test -f sounds/PIXABAY-CONTENT-LICENSE.md
	test -f sounds/LICENSES/CC-BY-SA-4.0.txt
	test -f sounds/LICENSES/CC-BY-4.0.txt
	test -f sounds/LICENSES/CC0-1.0.txt
	printf '%s\n' \
		'22626d303bd0939c5ad0afd6300669922426cd9bbe7155d2815faff56c05a17d  sounds/LICENSES/CC-BY-SA-4.0.txt' \
		'd557539df68e771cc1eedcc91d13f70fca930e508d11eedcafa4b15db49e3744  sounds/LICENSES/CC-BY-4.0.txt' \
		'a2010f343487d3f7618affe54f789f5487602331c0a8d03f49e9a7c547cf0499  sounds/LICENSES/CC0-1.0.txt' \
		'9dd354243ce155dff84ebba498dcd8c4abd8c2e6fa8b143f4fad2f8b11a53929  sounds/PIXABAY-CONTENT-LICENSE.md' \
		'868716bbd51231a9f1be986f94cc441e2fd5ba61877a10abad8cbde356505a11  sounds/bell.wav' \
		'fa13f711d61b01a6db2f9097159019606e14d0ef8075a00def7bb97ae8bdd332  sounds/click.wav' \
		'8b54813baa31e51324e865aed8c5dfd6ecd674bab87236a8fb1df301cb92a7ae  sounds/knock.wav' \
		'566d8941cdb6e40188de940e95075c38df2ea4dc3ce27dbca8cffdf534ef9f3f  sounds/pop.wav' \
		'a9ab74fc6c2ef116628bb370017cd4d1641c33a56ffee8c11788a7014953dd75  sounds/soft.wav' \
		'0e283f3de90a2cc52a4239430bd5cdd82c6e7df4d69af00abae6d07c6e4f0933  sounds/aurora.oga' \
		'c72acf3bfcbe6f46bd2a91376ee561ef8ed2dfa41b9fecbdf51b78b58a868b40  sounds/crystal.oga' \
		'4a75ec07365aee8dded5a65125b8540d5913ff659083e7f02360ba78b6680b20  sounds/glow.oga' \
		'16a4b5c49d9d01bcf2e46ad6c789f6849802447103776d8b1fe58cf1348ca024  sounds/halo.oga' \
		'97f5477244c3912a1ee9cb9200ec8daa9701aa01d6a658e118d7a80c49e593c6  sounds/msn.oga' \
		'84eb3dd0376dab5fd6cb54dca8d11e8576bc10e42809007e814a1e7474972015  sounds/phone.oga' \
		'e4694324dbf00d407a0475ff166b284cc2919a5c940bfafea5765d14a0923710  sounds/qq.oga' \
		'113094308abc743f8e525d346c9c33c61ffe91c7f494d4a1a522b5d05168038f  sounds/ripple.oga' \
		'd434cf3ad9c565605c2aa4b72b4cb51d4b6662a34526ec88f6b1551d08cd1e6a  sounds/skype.oga' \
		'36258b0046399c721eefa535c13a78d08b54ff1cec98a10db87d89d098760dd0  sounds/wechat.oga' | sha256sum -c -
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
	rm -f $(BIN_TEST) $(BIN_SPOOL_TEST) $(BIN_FILE_TRANSFER_TEST) $(BIN_AV_STATE_TEST) \
		$(BIN_RATCHET_PREKEY_TEST) $(BIN_IDENTITY_GUARD_TEST) \
		$(BIN_IPC_TEST_HELPER) $(BIN_HELP)
