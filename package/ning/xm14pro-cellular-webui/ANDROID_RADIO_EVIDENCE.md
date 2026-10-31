# Android radio evidence used by the native LuCI design

Reference capture:
`04-CPE/安卓参考/OS3.0.308.0/蜂窝拨号分析/android-full-cellular-20261011-104249`

| Function | Evidence | Confirmed behavior |
| --- | --- | --- |
| Physical SIM slots | `101-android-uim-get-card-status.txt`, `102-android-uim-get-slot-status.txt` | Two physical slots, slot/application mapping, ready USIM, provisioning sessions |
| Hot plug / slot migration | `92-slot1-to-slot2-state-timeline.txt` | `LOADED,ABSENT` → both absent → `ABSENT,READY` → `ABSENT,LOADED`; default-data subscription changed |
| Airplane mode | `52-flight-mode-state-timeline.txt` | Registered NR-SA → detached/operator cleared → registered again |
| Mobile data | `82-mobile-data-toggle-state-timeline.txt` | Data and rmnet route removed and restored |
| Dial profiles | `../radio-拨号日志.txt` | Internet/IMS/emergency profiles, initial attach APN and successful `SETUP_DATA_CALL` |
| IMS registration / SMS | `71-voice-sms-radio-follow.txt` | IMS registered; voice/video/UT/SMS capabilities; successful IMS SMS result |
| Voice call | `73-voice-sms-radio-final.txt` | Dialing, alerting, active and disconnected call states |

These files prove the stock Android path. They are design input, not live nwrt
state. The LuCI backend must still obtain every displayed runtime value and
execute every action through a verified nwrt transport.
