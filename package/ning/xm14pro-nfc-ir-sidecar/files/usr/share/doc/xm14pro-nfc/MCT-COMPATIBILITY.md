# MIFARE Classic Tool compatibility

The copy-mode MIFARE Classic workflow is interoperable with the text dump
format and the three standard keys used by MIFARE Classic Tool (MCT):

- Upstream: https://github.com/ikarus23/MifareClassicTool
- Reviewed revision: `b1ba0c4dd4bf873dc41c68a5fe337e8607b1414d`
- Upstream license: GPL-3.0-or-later

MCT is an Android application which calls `android.nfc.tech.MifareClassic`.
nwrt has no Android application framework, so the APK cannot run here. This
package instead provides an independently implemented compatibility adapter
over Xiaomi's stock NXP NFC HAL and NFA stack. No MCT Java source is included
in the proprietary sidecar package.

Copy mode automatically distinguishes standard NDEF tags from MIFARE Classic.
For Classic tags it creates an MCT-compatible `+Sector:` dump, authenticates
with the known standard keys, reads accessible sectors, then writes accessible
blocks to a same-size target tag. It validates sector-trailer access bytes and
writes trailers last.

The adapter intentionally does not crack or brute-force keys and never writes
manufacturer block 0. Consequently, the target tag keeps its own UID. Use it
only with tags you own or are authorized to test.
