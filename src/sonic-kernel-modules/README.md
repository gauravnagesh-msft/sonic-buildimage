# SONiC common kernel modules

This directory contains platform-independent, out-of-tree kernel modules built
against the exact SONiC kernel headers selected by `rules/linux-kernel.mk`.

## Build

Configure the build for the target platform before building the module:

```bash
PLATFORM=<platform> PLATFORM_ARCH=<arch> make configure
```

Build the unsigned runtime kdump proof-of-concept module:

```bash
make PLATFORM=<platform> PLATFORM_ARCH=<arch> kdump_runtime.ko
```

The equivalent path-form target is:

```bash
make target/files/<build-distro>/kdump_runtime.ko
```

For example, a Debian 13/Trixie build uses:

```bash
make target/files/trixie/kdump_runtime.ko
```

Build a signed copy:

```bash
make kdump_runtime.signed.ko \
    SECURE_UPGRADE_SIGNING_CERT=/path/to/signing-cert.pem \
    SECURE_UPGRADE_DEV_SIGNING_KEY=/path/to/signing-key.pem
```

The equivalent path-form target is:

```bash
make target/files/<build-distro>/kdump_runtime.signed.ko \
    SECURE_UPGRADE_SIGNING_CERT=/path/to/signing-cert.pem \
    SECURE_UPGRADE_DEV_SIGNING_KEY=/path/to/signing-key.pem
```

The unsigned and signed artifacts are written under
`target/files/<build-distro>/`. The signed target rebuilds the module cleanly,
signs a temporary copy with `scripts/signing_kernel_modules.sh`, and leaves the
unsigned artifact unchanged.

The certificate and private key must correspond to a certificate trusted by
the target device when module-signature enforcement is enabled. A disposable
development certificate can verify the build and signing flow, but it does not
make the module loadable on a device that does not trust that certificate.
Never commit private signing keys.

## Validate against the target switch

The module must be built from the SONiC source revision matching the target
image. Before loading it, compare the switch kernel release with the module
vermagic:

```bash
uname -r
modinfo ./kdump_runtime.ko | grep '^vermagic:'
```

The vermagic kernel release must exactly match `uname -r`. Signing does not
make an ABI-mismatched module loadable.

Use `stage=0` for the initial load test. It verifies module loading and symbol
version compatibility without allocating memory or changing crash-kernel
resources:

```bash
sudo insmod ./kdump_runtime.ko stage=0
lsmod | grep '^kdump_runtime'
sudo dmesg | tail -n 20
sudo rmmod kdump_runtime
```

To test with `modprobe`, first install the module in the running kernel's module
tree and regenerate dependencies:

```bash
sudo install -D -m 0644 ./kdump_runtime.ko \
    /lib/modules/$(uname -r)/extra/kdump_runtime.ko
sudo depmod -a
sudo modprobe kdump_runtime stage=0
sudo modprobe -r kdump_runtime
```
