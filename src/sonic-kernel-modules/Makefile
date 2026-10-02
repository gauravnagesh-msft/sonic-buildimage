.ONESHELL:
SHELL = /bin/bash
.SHELLFLAGS += -e

KERNEL_BUILD := /lib/modules/$(KVERSION)/build
MODULE_DIR := $(CURDIR)/kdump-runtime
MODULE_NAME := kdump_runtime.ko
SIGNING_SCRIPT := $(PROJECT_ROOT)/scripts/signing_kernel_modules.sh
SIGNING_KBUILD_VERSION ?= $(KERNEL_VERSION)$(KERNEL_ABISUFFIX)

define build_module
	test -d "$(KERNEL_BUILD)"
	$(MAKE) -C "$(KERNEL_BUILD)" M="$(MODULE_DIR)" clean
	$(MAKE) -C "$(KERNEL_BUILD)" M="$(MODULE_DIR)" modules
	test -f "$(MODULE_DIR)/$(MODULE_NAME)"
endef

$(DEST)/kdump_runtime.ko:
	$(build_module)
	install -m 0644 "$(MODULE_DIR)/$(MODULE_NAME)" "$@"

$(DEST)/kdump_runtime.signed.ko:
	test -n "$(SECURE_UPGRADE_SIGNING_CERT)" || { \
		echo "SECURE_UPGRADE_SIGNING_CERT is required"; \
		exit 1; \
	}
	test -n "$(SECURE_UPGRADE_DEV_SIGNING_KEY)" || { \
		echo "SECURE_UPGRADE_DEV_SIGNING_KEY is required"; \
		exit 1; \
	}
	test -n "$(SIGNING_KBUILD_VERSION)"
	test -f "$(SECURE_UPGRADE_SIGNING_CERT)"
	test -f "$(SECURE_UPGRADE_DEV_SIGNING_KEY)"
	test -x "$(SIGNING_SCRIPT)"
	$(build_module)
	if grep -q "~Module signature appended~" "$(MODULE_DIR)/$(MODULE_NAME)"; then \
		echo "$(MODULE_NAME) was already signed during compilation"; \
		exit 1; \
	fi
	tmpdir=$$(mktemp -d)
	trap 'rm -rf "$$tmpdir"' EXIT
	install -m 0644 "$(MODULE_DIR)/$(MODULE_NAME)" "$$tmpdir/$(MODULE_NAME)"
	"$(SIGNING_SCRIPT)" \
		-l "$(SIGNING_KBUILD_VERSION)" \
		-c "$(SECURE_UPGRADE_SIGNING_CERT)" \
		-p "$(SECURE_UPGRADE_DEV_SIGNING_KEY)" \
		-k "$$tmpdir"
	grep -q "~Module signature appended~" "$$tmpdir/$(MODULE_NAME)"
	install -m 0644 "$$tmpdir/$(MODULE_NAME)" "$@"
