# Xiaomi 14 Pro telephony sidecar

This package stages the stock Xiaomi `qcrilNrd` and IMS userspace inside the
existing Bionic compatibility root. It is separate from QWES and NICM because
secure storage, data dialing, and the Android telephony state machine are three
different runtime layers.

The package is intentionally disabled by default while bring-up is in progress.
The supported validation order is:

1. install without starting it;
2. validate the Android service manager in isolation;
3. validate both QCRIL instances and modem registration after a physical boot;
4. start the IMS services and establish the independent IMS bearer;
5. perform real SMS and voice tests;
6. enable the package by default only after the cold-boot test passes.

The binary files were extracted from the user's OS3.0.308.0 device backup. The
dependency closure includes QCRIL modules loaded with `dlopen()`, not only ELF
`DT_NEEDED` entries. No carrier, APN, SIM slot, IMEI, or subscriber identity is
hard-coded in this package.
