# Photonicat 2 vendor stack

This directory packages the public Photonicat sources for the RK3576
Photonicat 2 image.  The application sources are downloaded at the pinned
commits in each package Makefile so builds remain reproducible.

Included packages:

- `pcat-manager`: official PMU and modem backend, including `fm350-mm`.
- `pcat-manager-web`: official Flask management UI.
- `pcat-sidecar`: loopback-only SMS/telemetry bridge with seven-day history.
- `pcat2-display-mini`: official front-panel display service.
- `photonicat-pm`: official kernel driver with a Linux 6.18 API-only patch.
- `quectel-cm`: Quectel helper used by the vendor modem backend.
- `python-pam`: login dependency required by the Web UI.

Default access after a fresh image boot:

- Photonicat vendor UI: `http://192.168.66.1/` (port 80)
- Native OpenWrt LuCI: `http://192.168.66.1:8080/`
- Native OpenWrt LuCI HTTPS: port 8443

The Web and display packages carry the compatible parts of the local vendor
firmware backup: the dark responsive UI, FM350 status panels, persistent
telemetry charts, SMS API fixes, and the display SMS inbox/notification.  The
sidecar reads the existing vendor SMS database and vendor loopback APIs; it
does not open or take ownership of any modem serial device.  The vendor
`pcat-manager` and `fm350-mm` dialing processes remain unchanged.

The runtime archive made from photonicatWrt 26.04.1 must not be restored over
this NWRT image wholesale.  Its Python entry point and templates target a
different vendor runtime.  Keep using these source packages when building an
NWRT image.

The older custom implementation remains in `../photonicat2`; it is not
selected by the Photonicat 2 image configuration.
