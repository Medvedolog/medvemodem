# Modem Zoo — extended AT reference

> **Reference-only appendix.** This page is generated from normalized QModem/other scripts evidence. A command appearing here is **not** a safety approval or proof that it applies to every modem of that vendor.

This appendix exists for completeness and searchability. Model pages should provide the stronger model/firmware-specific interpretation.

## QModem command wrappers

QModem evidence pin: `c49654efc870f53712ee8e25bf181722eb1466d9`.

Templates are shown as written in the upstream source (`$2`, `$CID` are script variables). Risk values below are the current import-time hints. They must be reviewed before any runtime promotion.

### generic

| AT template | Wrapper | Semantic hint | Risk hint |
|---|---|---|---|
| `AT+GTDNS=$2` | `cmd_gtdns` | data.session | unknown |
| `AT+CGACT?` | `cmd_cgact_query` | data.session | read-mostly |
| `AT+CGPADDR=$2` | `cmd_cgpaddr` | data.session | unknown |
| `AT+CFUN=1,1` | `cmd_cfun_soft_reboot` | modem.reset | mutating |
| `AT+CPMS?` | `cmd_cpms_query` | sms.storage | read-mostly |
| `AT+CPMS=\"$2\",\"$3\",\"$4\"` | `cmd_cpms_set` | sms.storage | mutating |
| `AT+CPMS=\"$2\",\"$3\"` | `cmd_cpms_set` | sms.storage | mutating |

### quectel

| AT template | Wrapper | Semantic hint | Risk hint |
|---|---|---|---|
| `AT+QCFG="5glan"` | `cmd_qcfg_5glan_query` | vendor.raw | read-mostly |
| `AT+QCFG=\"5glan\",1,$2` | `cmd_qcfg_5glan_set` | vendor.raw | mutating |
| `AT+CGSN` | `cmd_cgsn` | vendor.raw | read-mostly |
| `AT+EGMR=1,7,\"$2\"` | `cmd_egmr_set_imei` | identity.imei | mutating |
| `AT+QCFG="usbnet"` | `cmd_qcfg_usbnet_query` | transport.composition | read-mostly |
| `AT+QCFG=\"usbnet\",$2` | `cmd_qcfg_usbnet_set` | transport.composition | mutating |
| `AT+QCFG="nwscanmode"` | `cmd_qcfg_nwscanmode_query` | radio.mode | read-mostly |
| `AT+QCFG=\"nwscanmode\",$2` | `cmd_qcfg_nwscanmode_set` | radio.mode | mutating |
| `AT+QNWPREFCFG="mode_pref"` | `cmd_qnwprefcfg_mode_pref_query` | radio.mode | read-mostly |
| `AT+QNWPREFCFG=\"mode_pref\",$2` | `cmd_qnwprefcfg_mode_pref_set` | radio.mode | mutating |
| `AT+CBC` | `cmd_cbc` | vendor.raw | read-mostly |
| `AT+QTEMP` | `cmd_qtemp` | hardware.temperature | read-mostly |
| `AT+CGMM` | `cmd_cgmm` | identity.model | read-mostly |
| `AT+CGMI` | `cmd_cgmi` | identity.manufacturer | read-mostly |
| `ATI` | `cmd_ati` | identity.revision | read-mostly |
| `AT+QUIMSLOT?` | `cmd_quimslot_query` | sim.switch | read-mostly |
| `AT+QUIMSLOT=?` | `cmd_quimslot_list_query` | sim.switch | read-mostly |
| `AT+QUIMSLOT=$2` | `cmd_quimslot_set` | sim.switch | mutating |
| `AT+CPIN?` | `cmd_cpin_query` | sim.status | read-mostly |
| `AT+COPS=3,2` | `cmd_cops_numeric` | registration.operator | read-mostly |
| `AT+COPS?` | `cmd_cops_query` | registration.operator | read-mostly |
| `AT+CNUM` | `cmd_cnum` | sim.number | read-mostly |
| `AT+CIMI` | `cmd_cimi` | sim.imsi | read-mostly |
| `AT+ICCID` | `cmd_iccid` | sim.iccid | read-mostly |
| `AT+CCID` | `cmd_ccid` | sim.iccid | read-mostly |
| `AT+QNWINFO` | `cmd_qnwinfo` | radio.serving | read-mostly |
| `AT+CSQ` | `cmd_csq` | radio.signal | read-mostly |
| `AT+QNWCFG="nr5g_ambr"` | `cmd_qnwcfg_nr5g_ambr_query` | traffic.stats | read-mostly |
| `AT+QNWCFG="up/down"` | `cmd_qnwcfg_updown_query` | traffic.stats | read-mostly |
| `AT+QNWPREFCFG=\"$2\"` | `cmd_qnwprefcfg_band_query` | radio.band | read-mostly |
| `AT+QNWPREFCFG=\"$2\",$3` | `cmd_qnwprefcfg_band_set` | radio.band | mutating |
| `AT+QCFG=\"band\"` | `cmd_qcfg_band_query` | radio.band | read-mostly |
| `AT+QCFG=\"band\",0,$2,0` | `cmd_qcfg_band_reset` | radio.band | mutating |
| `AT+QENG="neighbourcell"` | `cmd_qeng_neighbourcell` | cell.neighbors | unknown |
| `AT+QENG="servingcell"` | `cmd_qeng_servingcell` | cell.serving_or_lock | unknown |
| `AT+QCAINFO` | `cmd_qcainfo` | radio.serving | read-mostly |
| `AT+QNWLOCK=\"$2\"` | `cmd_qnwlock_query` | vendor.raw | read-mostly |
| `AT+QNWLOCK=\"$2\",0` | `cmd_qnwlock_unlock` | vendor.raw | mutating |
| `AT+QNWLOCK=\"$2\",$3` | `cmd_qnwlock_set` | vendor.raw | mutating |
| `AT+QGDCNT?` | `cmd_qgdcnt_query` | traffic.stats | read-mostly |
| `AT+QGDNRCNT?` | `cmd_qgdnrcnt_query` | vendor.raw | read-mostly |
| `AT+QAUGDCNT=$2` | `cmd_qaugdcnt_set` | traffic.stats | mutating |
| `AT+QGDNRCNT=$2` | `cmd_qgdnrcnt_set` | vendor.raw | mutating |
| `AT+QGDCNT=$2` | `cmd_qgdcnt_set` | traffic.stats | mutating |

### fibocom

| AT template | Wrapper | Semantic hint | Risk hint |
|---|---|---|---|
| `AT+GTUSBMODE?` | `cmd_gtusbmode_query` | transport.composition | read-mostly |
| `AT+GTUSBMODE=$2` | `cmd_gtusbmode_set` | transport.composition | mutating |
| `AT+GTACT?` | `cmd_gtact_query` | vendor.raw | read-mostly |
| `AT+GTACT=?` | `cmd_gtact_list_query` | vendor.raw | read-mostly |
| `AT+GTACT=$2` | `cmd_gtact_set` | vendor.raw | mutating |
| `AT+CBC` | `cmd_cbc` | vendor.raw | read-mostly |
| `AT+MTSM=1,6` | `cmd_mtsm_1_6` | vendor.raw | unknown |
| `AT+GTLADC` | `cmd_gtladc` | hardware.temperature | unknown |
| `AT+GTSENRDTEMP=1` | `cmd_gtsenrdtemp` | hardware.temperature | read-mostly |
| `AT+MTSM=1` | `cmd_mtsm` | vendor.raw | unknown |
| `AT+CGMM?` | `cmd_cgmm` | identity.model | read-mostly |
| `AT+CGMI?` | `cmd_cgmi` | identity.manufacturer | read-mostly |
| `AT+CGMR?` | `cmd_cgmr` | identity.revision | read-mostly |
| `AT+GTDUALSIM?` | `cmd_gtdualsim_query` | sim.switch | read-mostly |
| `AT+GTDUALSIM=$2` | `cmd_gtdualsim_set` | sim.switch | mutating |
| `AT+CGSN?` | `cmd_cgsn` | vendor.raw | read-mostly |
| `AT+CPIN?` | `cmd_cpin_query` | sim.status | read-mostly |
| `AT+COPS?` | `cmd_cops_query` | registration.operator | read-mostly |
| `AT+GTCURCAR?` | `cmd_gtcurcar_query` | radio.serving | read-mostly |
| `AT+CNUM` | `cmd_cnum` | sim.number | read-mostly |
| `AT+CIMI?` | `cmd_cimi` | sim.imsi | read-mostly |
| `AT+ICCID` | `cmd_iccid` | sim.iccid | read-mostly |
| `AT+CCID` | `cmd_ccid` | sim.iccid | read-mostly |
| `AT+GTSN=1,7,\"$2\"` | `cmd_gtsn_set_imei` | identity.imei | mutating |
| `AT+EGMREXT=1,7,\"$2\"` | `cmd_egmrext_set_imei` | identity.imei | mutating |
| `AT+LCTSN=1,7,\"$2\"` | `cmd_lctsn_set_imei` | identity.imei | mutating |
| `AT+PSRAT?` | `cmd_psrat_query` | vendor.raw | read-mostly |
| `AT+GTSTATIS?` | `cmd_gtstatis_query` | traffic.stats | read-mostly |
| `AT+GTCCINFO?` | `cmd_gtccinfo_query` | radio.serving | read-mostly |
| `AT+GTCAINFO?` | `cmd_gtcainfo_query` | radio.serving | read-mostly |
| `AT+GTCELLLOCK?` | `cmd_gtcelllock_query` | cell.serving_or_lock | read-mostly |
| `AT+GTCELLLOCK=$2` | `cmd_gtcelllock_set` | cell.serving_or_lock | mutating |
| `AT+CSQ` | `cmd_csq` | radio.signal | read-mostly |
| `AT+GTUSAGEREC?` | `cmd_gtusagerec_query` | traffic.stats | read-mostly |
| `AT+GTUSAGEREC` | `cmd_gtusagerec` | traffic.stats | unknown |

### foxconn

| AT template | Wrapper | Semantic hint | Risk hint |
|---|---|---|---|
| `ATI` | `cmd_ati` | identity.revision | read-mostly |
| `AT+CPIN?` | `cmd_cpin_query` | sim.status | read-mostly |
| `AT+COPS?` | `cmd_cops_query` | registration.operator | read-mostly |
| `AT+COPS=3,2` | `cmd_cops_numeric` | registration.operator | read-mostly |
| `AT+CNUM` | `cmd_cnum` | sim.number | read-mostly |
| `AT+CIMI` | `cmd_cimi` | sim.imsi | read-mostly |
| `AT+ICCID` | `cmd_iccid` | sim.iccid | read-mostly |
| `AT!PCVOLT?` | `cmd_pcvolt_query` | vendor.raw | read-mostly |

### sierra

| AT template | Wrapper | Semantic hint | Risk hint |
|---|---|---|---|
| `AT!ENTERCND=\"$2\"` | `cmd_entercnd` | vendor.raw | unknown |
| `AT+CGSN` | `cmd_cgsn` | vendor.raw | read-mostly |
| `AT+EGMR=1,7,\"$2\"` | `cmd_egmr_set_imei` | identity.imei | mutating |
| `AT!USBCOMP?` | `cmd_usbcomp_query` | transport.composition | read-mostly |
| `AT!USBCOMP=1,4,$2` | `cmd_usbcomp_set` | transport.composition | mutating |
| `AT!SELRAT=$2` | `cmd_selrat_set` | radio.mode | mutating |
| `AT!UIMS?` | `cmd_uims_query` | vendor.raw | read-mostly |
| `AT+CPIN?` | `cmd_cpin_query` | sim.status | read-mostly |
| `AT+CGMM` | `cmd_cgmm` | identity.model | read-mostly |
| `AT+CGMI` | `cmd_cgmi` | identity.manufacturer | read-mostly |
| `ATI` | `cmd_ati` | identity.revision | read-mostly |
| `AT!GSTATUS?` | `cmd_gstatus_query` | radio.serving | read-mostly |
| `AT!BAND?` | `cmd_band_query` | radio.band | read-mostly |
| `AT!BAND=?` | `cmd_band_list_query` | radio.band | read-mostly |
| `AT!BAND=0F,1,\"Custom\",$2,$3` | `cmd_band_set_custom` | radio.band | mutating |
| `AT!BAND=$2` | `cmd_band_reset` | radio.band | mutating |
| `AT!PCVOLT?` | `cmd_pcvolt_query` | vendor.raw | read-mostly |
| `AT!PCTEMP?` | `cmd_pctemp_query` | hardware.temperature | read-mostly |

### simcom

| AT template | Wrapper | Semantic hint | Risk hint |
|---|---|---|---|
| `AT+CGSN` | `cmd_cgsn` | vendor.raw | read-mostly |
| `AT+SIMEI=$2` | `cmd_simei_set` | identity.imei | mutating |
| `AT+CUSBCFG?` | `cmd_cusbcfg_query` | transport.composition | read-mostly |
| `AT+CUSBCFG=usbid,1e0e,$2` | `cmd_cusbcfg_set_usbid` | transport.composition | mutating |
| `AT+CPCIEMODE?` | `cmd_cpciemode_query` | transport.composition | read-mostly |
| `AT$MYCONFIG?` | `cmd_myconfig_query` | vendor.raw | read-mostly |
| `AT+CNMP?` | `cmd_cnmp_query` | radio.mode | read-mostly |
| `AT+CNMP=$2` | `cmd_cnmp_set` | radio.mode | mutating |
| `AT+CBC` | `cmd_cbc` | vendor.raw | read-mostly |
| `AT+CPMUTEMP` | `cmd_cpmutemp` | hardware.temperature | read-mostly |
| `AT+CGMM` | `cmd_cgmm` | identity.model | read-mostly |
| `AT+CGMI` | `cmd_cgmi` | identity.manufacturer | read-mostly |
| `AT+SIMCOMATI` | `cmd_simcomati` | identity.revision | read-mostly |
| `AT+SMSIMCFG?` | `cmd_smsimcfg_query` | vendor.raw | read-mostly |
| `AT+CPIN?` | `cmd_cpin_query` | sim.status | read-mostly |
| `AT+COPS?` | `cmd_cops_query` | registration.operator | read-mostly |
| `AT+CNUM` | `cmd_cnum` | sim.number | read-mostly |
| `AT+CIMI` | `cmd_cimi` | sim.imsi | read-mostly |
| `AT+ICCID` | `cmd_iccid` | sim.iccid | read-mostly |
| `AT+CPSI?` | `cmd_cpsi_query` | radio.serving | read-mostly |
| `AT+CSYSSEL=\"$2\"` | `cmd_csyssel_query` | radio.mode | read-mostly |
| `AT+CSYSSEL=\"$2\",$3` | `cmd_csyssel_set` | radio.mode | mutating |
| `AT+CNBP?` | `cmd_cnbp_query` | vendor.raw | read-mostly |
| `AT+CNBP=$2` | `cmd_cnbp_set` | vendor.raw | mutating |
| `AT+CCELLCFG?` | `cmd_ccellcfg_query` | cell.serving_or_lock | read-mostly |
| `AT+C5GCELLCFG?` | `cmd_c5gcellcfg_query` | cell.serving_or_lock | read-mostly |
| `AT+CCELLCFG=0` | `cmd_ccellcfg_unlock` | cell.serving_or_lock | mutating |
| `AT+C5GCELLCFG="unlock"` | `cmd_c5gcellcfg_unlock` | cell.serving_or_lock | mutating |
| `AT+CCELLCFG=1,$2,$3;+CNMP=38` | `cmd_ccellcfg_lock` | cell.serving_or_lock | mutating |
| `AT+C5GCELLCFG=\"pci\",$2,$3,$4,$5;+CNMP=71` | `cmd_c5gcellcfg_lock` | cell.serving_or_lock | mutating |
| `AT+CNWSEARCH=\"$2\"` | `cmd_cnwsearch_query` | vendor.raw | read-mostly |
| `AT+CNWSEARCH=\"$2\",$3` | `cmd_cnwsearch_scan` | vendor.raw | unknown |
| `AT+CNWINFO?` | `cmd_cnwinfo_query` | radio.serving | read-mostly |

### huawei

| AT template | Wrapper | Semantic hint | Risk hint |
|---|---|---|---|
| `AT+CGSN` | `cmd_cgsn` | vendor.raw | read-mostly |
| `AT^SETMODE?` | `cmd_setmode_query` | radio.mode | mutating |
| `AT^SETMODE=$2` | `cmd_setmode_set` | radio.mode | mutating |
| `AT^SYSCFGEX?` | `cmd_syscfgex_query` | radio.mode | read-mostly |
| `AT^SYSCFGEX=\"$2\",40000000,1,2,40000000,,` | `cmd_syscfgex_set` | radio.mode | mutating |
| `AT+CPIN?` | `cmd_cpin_query` | sim.status | read-mostly |
| `AT+CNUM` | `cmd_cnum` | sim.number | read-mostly |
| `AT+CIMI` | `cmd_cimi` | sim.imsi | read-mostly |
| `AT+CGMM` | `cmd_cgmm` | identity.model | read-mostly |
| `AT+CGMI` | `cmd_cgmi` | identity.manufacturer | read-mostly |
| `ATI` | `cmd_ati` | identity.revision | read-mostly |
| `AT^MONSC` | `cmd_monsc` | vendor.raw | unknown |
| `AT^CSERSSI?` | `cmd_cserssi_query` | vendor.raw | read-mostly |
| `AT^HFREQINFO?` | `cmd_hfreqinfo_query` | vendor.raw | read-mostly |
| `AT!BAND?` | `cmd_band_query` | radio.band | read-mostly |
| `AT!BAND=?` | `cmd_band_list_query` | radio.band | read-mostly |
| `AT!BAND=0F,1,\"Custom\",$2,$3` | `cmd_band_set_custom` | radio.band | mutating |
| `AT!BAND=$2` | `cmd_band_reset` | radio.band | mutating |
| `AT^CHIPTEMP?` | `cmd_chiptemp_query` | hardware.temperature | read-mostly |
| `AT^SIMSWITCH=$2` | `cmd_simswitch_set` | vendor.raw | mutating |
| `AT^SCICHG=$2,$3` | `cmd_scichg` | vendor.raw | unknown |

### meig

| AT template | Wrapper | Semantic hint | Risk hint |
|---|---|---|---|
| `AT+CGSN` | `cmd_cgsn` | vendor.raw | read-mostly |
| `AT+LCTSN=1,7,\"$2\"` | `cmd_lctsn_set_imei` | identity.imei | mutating |
| `AT+SER?` | `cmd_ser_query` | transport.composition | read-mostly |
| `AT+SER=$2,1` | `cmd_ser_set` | transport.composition | mutating |
| `AT^SYSCFGEX?` | `cmd_syscfgex_query` | radio.mode | read-mostly |
| `AT^SYSCFGEX=\"$2\",all,0,2,all,all,all,all,1` | `cmd_syscfgex_set` | radio.mode | mutating |
| `AT+TEMP` | `cmd_temp` | hardware.temperature | read-mostly |
| `AT+CGMM` | `cmd_cgmm` | identity.model | read-mostly |
| `AT+CGMI` | `cmd_cgmi` | identity.manufacturer | read-mostly |
| `AT+CGMR` | `cmd_cgmr` | identity.revision | read-mostly |
| `AT^SIMSLOT?` | `cmd_sims_slot_query` | sim.switch | read-mostly |
| `AT+CPIN?` | `cmd_cpin_query` | sim.status | read-mostly |
| `AT+COPS?` | `cmd_cops_query` | registration.operator | read-mostly |
| `AT+CNUM` | `cmd_cnum` | sim.number | read-mostly |
| `AT+CIMI` | `cmd_cimi` | sim.imsi | read-mostly |
| `AT+ICCID` | `cmd_iccid` | sim.iccid | read-mostly |
| `AT^SYSINFOEX` | `cmd_sysinfoex` | vendor.raw | read-mostly |
| `AT+CSQ` | `cmd_csq` | radio.signal | read-mostly |
| `AT^DSAMBR=$2` | `cmd_dsambr` | traffic.stats | unknown |
| `AT^DSFLOWQRY` | `cmd_dsflowqry` | traffic.stats | unknown |
| `AT^CELLINFO=$2` | `cmd_cellinfo` | cell.serving_or_lock | read-mostly |

### telit

| AT template | Wrapper | Semantic hint | Risk hint |
|---|---|---|---|
| `AT#USBCFG?` | `cmd_usbcfg_query` | transport.composition | read-mostly |
| `AT#USBCFG=$2` | `cmd_usbcfg_set` | transport.composition | mutating |
| `AT+WS46?` | `cmd_ws46_query` | radio.mode | read-mostly |
| `AT+WS46=$2` | `cmd_ws46_set` | radio.mode | mutating |
| `AT#CBC` | `cmd_cbc` | vendor.raw | read-mostly |
| `AT#TEMPSENS=2` | `cmd_tempsens` | hardware.temperature | read-mostly |
| `AT+CGMM` | `cmd_cgmm` | identity.model | read-mostly |
| `AT+CGMI` | `cmd_cgmi` | identity.manufacturer | read-mostly |
| `AT+CGMR` | `cmd_cgmr` | identity.revision | read-mostly |
| `AT#QSS?` | `cmd_qss_query` | vendor.raw | read-mostly |
| `AT+CGSN` | `cmd_cgsn` | vendor.raw | read-mostly |
| `AT+CPIN?` | `cmd_cpin_query` | sim.status | read-mostly |
| `AT+COPS?` | `cmd_cops_query` | registration.operator | read-mostly |
| `AT+CIMI` | `cmd_cimi` | sim.imsi | read-mostly |
| `AT+ICCID` | `cmd_iccid` | sim.iccid | read-mostly |
| `AT#CAMETRICS=1;#CAMETRICS?` | `cmd_cametrics` | vendor.raw | unknown |
| `AT#CQI` | `cmd_cqi` | vendor.raw | unknown |
| `AT#BND?` | `cmd_bnd_query` | vendor.raw | read-mostly |
| `AT#BND=?` | `cmd_bnd_list_query` | vendor.raw | read-mostly |
| `AT#BND=0,22,$2` | `cmd_bnd_set` | vendor.raw | mutating |
| `AT#CAINFOEXT?` | `cmd_cainfoext_query` | radio.serving | read-mostly |

### neoway

| AT template | Wrapper | Semantic hint | Risk hint |
|---|---|---|---|
| `AT+CGSN` | `cmd_cgsn` | vendor.raw | read-mostly |
| `AT+SPIMEI=0,\"$2\"` | `cmd_spimei_set` | identity.imei | mutating |
| `AT$MYSYSINFO` | `cmd_mysysinfo_query` | radio.mode | read-mostly |
| `AT+CGMM` | `cmd_cgmm` | identity.model | read-mostly |
| `AT+CGMI` | `cmd_cgmi` | identity.manufacturer | read-mostly |
| `ATI` | `cmd_ati` | identity.revision | read-mostly |
| `AT+SIMCROSS?` | `cmd_simcross_query` | vendor.raw | read-mostly |
| `AT+CPIN?` | `cmd_cpin_query` | sim.status | read-mostly |
| `AT+COPS?` | `cmd_cops_query` | registration.operator | read-mostly |
| `AT+CNUM` | `cmd_cnum` | sim.number | read-mostly |
| `AT+CIMI` | `cmd_cimi` | sim.imsi | read-mostly |
| `AT$MYCCID` | `cmd_myccid` | sim.iccid | read-mostly |
| `AT+CSQ` | `cmd_csq` | radio.signal | read-mostly |
| `AT+C5GQOSRDP` | `cmd_c5gqosrdp` | vendor.raw | unknown |
| `AT+NWSETBAND?` | `cmd_nwsetband_query` | radio.band | read-mostly |
| `AT+NWSETBAND=?` | `cmd_nwsetband_list_query` | radio.band | read-mostly |
| `AT+NWSETBAND=0` | `cmd_nwsetband_reset` | radio.band | mutating |
| `AT+NWSETBAND=$2,$3$4` | `cmd_nwsetband_set` | radio.band | mutating |
| `AT+NETDMSGEX` | `cmd_netdmsgex` | vendor.raw | unknown |

### thales

| AT template | Wrapper | Semantic hint | Risk hint |
|---|---|---|---|
| `ATI` | `cmd_ati` | identity.revision | read-mostly |
| `AT+CGMM` | `cmd_cgmm` | identity.model | read-mostly |
| `AT+CGMI` | `cmd_cgmi` | identity.manufacturer | read-mostly |
| `AT+CGSN` | `cmd_cgsn` | vendor.raw | read-mostly |
| `AT+CPIN?` | `cmd_cpin_query` | sim.status | read-mostly |
| `AT+COPS=3,2` | `cmd_cops_numeric` | registration.operator | read-mostly |
| `AT+COPS?` | `cmd_cops_query` | registration.operator | read-mostly |
| `AT+CNUM` | `cmd_cnum` | sim.number | read-mostly |
| `AT+CIMI` | `cmd_cimi` | sim.imsi | read-mostly |
| `AT+ICCID` | `cmd_iccid` | sim.iccid | read-mostly |
| `AT+SETCONFIG?` | `cmd_setconfig_query` | vendor.raw | mutating |
| `AT+SETCONFIG=$2` | `cmd_setconfig_set` | vendor.raw | mutating |
| `AT+MODESWITCH?` | `cmd_modeswitch_query` | transport.composition | mutating |
| `AT+MODESWITCH=$2` | `cmd_modeswitch_set` | transport.composition | mutating |
| `AT^SLMODE?` | `cmd_slmode_query` | vendor.raw | read-mostly |
| `AT^SLMODE=1,$2` | `cmd_slmode_set` | vendor.raw | mutating |
| `AT+SWITCH_SLOT?` | `cmd_switch_slot_query` | sim.switch | mutating |
| `AT+SWITCH_SLOT=$2` | `cmd_switch_slot_set` | sim.switch | mutating |
| `AT+BAND_PREF?` | `cmd_band_pref_query` | radio.band | read-mostly |
| `AT+BAND_PREF=$2,2,$3` | `cmd_band_pref_lock` | radio.band | mutating |
| `AT^TEMP?` | `cmd_temp_query` | hardware.temperature | read-mostly |
| `AT^DEBUG?` | `cmd_debug_query` | vendor.raw | read-mostly |
| `AT+RESET` | `cmd_reset` | modem.reset | mutating |

### gosuncn

| AT template | Wrapper | Semantic hint | Risk hint |
|---|---|---|---|
| `AT+CGSN` | `cmd_cgsn` | vendor.raw | read-mostly |
| `AT+EGMR=1,7,\"$2\"` | `cmd_egmr_set_imei` | identity.imei | mutating |
| `AT+ZSWITCH?` | `cmd_zswitch_query` | vendor.raw | mutating |
| `AT+ZSWITCH=$2` | `cmd_zswitch_set` | vendor.raw | mutating |
| `AT+ZSNT?` | `cmd_zsnt_query` | radio.mode | read-mostly |
| `AT+ZSNT=$2` | `cmd_zsnt_set` | radio.mode | mutating |
| `AT+ZSNT=0,0,0` | `cmd_zsnt_reset` | radio.mode | mutating |
| `AT+ZPAS?` | `cmd_zpas_query` | radio.serving | read-mostly |
| `AT+C5GREG?` | `cmd_c5greg_query` | vendor.raw | read-mostly |
| `AT+MTSM=1` | `cmd_mtsm` | vendor.raw | unknown |
| `AT+ZBAND?` | `cmd_zband_query` | radio.band | read-mostly |
| `AT+ZBAND=?` | `cmd_zband_list_query_lte` | radio.band | read-mostly |
| `AT+ZBAND=all,all,all,all` | `cmd_zband_reset_all_lte` | radio.band | mutating |
| `AT+ZBAND=0` | `cmd_zband_reset_all_qualcomm` | radio.band | mutating |
| `AT+ZBAND=all,all,all,$2` | `cmd_zband_set_lte` | radio.band | mutating |
| `AT+ZBAND=$2,$3,$4` | `cmd_zband_set_qualcomm` | radio.band | mutating |
| `AT+ZLOCKCELL?` | `cmd_zlockcell_query` | cell.serving_or_lock | read-mostly |
| `AT+ZLOCKCELL=1,1,$2,$3` | `cmd_zlockcell_set_lte` | cell.serving_or_lock | mutating |
| `AT+ZLOCKCELL=1,2,$2,$3,$4,$5` | `cmd_zlockcell_set_nr` | cell.serving_or_lock | mutating |
| `AT+ZLOCKCELL=0` | `cmd_zlockcell_unlock` | cell.serving_or_lock | mutating |
| `AT+CPIN?` | `cmd_cpin_query` | sim.status | read-mostly |
| `AT+COPS=3,2` | `cmd_cops_numeric` | registration.operator | read-mostly |
| `AT+COPS?` | `cmd_cops_query` | registration.operator | read-mostly |
| `AT+CNUM` | `cmd_cnum` | sim.number | read-mostly |
| `AT+CIMI` | `cmd_cimi` | sim.imsi | read-mostly |
| `AT+ICCID` | `cmd_iccid` | sim.iccid | read-mostly |
| `AT+CGMM` | `cmd_cgmm` | identity.model | read-mostly |
| `AT+CGMI` | `cmd_cgmi` | identity.manufacturer | read-mostly |
| `AT+CGMR` | `cmd_cgmr` | identity.revision | read-mostly |
| `AT+CSQ` | `cmd_csq` | radio.signal | read-mostly |
| `AT+ZCELLINFO?` | `cmd_zcellinfo_query` | cell.serving_or_lock | read-mostly |
| `AT+CESQ` | `cmd_cesq` | radio.signal | unknown |

## other scripts field command corpus

other scripts evidence pin: `cc041cb09336b4e85f6b4fba4b75f0d604e1ecc3`.

These commands are grouped by the original script where they were observed. They are valuable as field history, not as a recommendation to recreate other scripts orchestration.

### Common / identity / signal

#### `…/fibocomdata.sh`

| AT template | Risk hint |
|---|---|
| `AT+COPN` | unknown |
| `AT+GTZONERDMAXTEMP=1` | unknown |

#### `…/fibocomdata350.sh`

| AT template | Risk hint |
|---|---|
| `AT+COPN` | unknown |

#### `…/gettype.sh`

| AT template | Risk hint |
|---|---|
| `AT+CASIMS=1` | mutating-or-session-affecting |
| `AT+CASIMS?` | read-mostly |
| `AT+CCID` | unknown |
| `AT+CGDCONT?` | read-mostly |
| `AT+CGMI` | read-mostly |
| `AT+CGMM` | read-mostly |
| `AT+CGSN` | read-mostly |
| `AT+CIMI` | read-mostly |
| `AT+CRSM=176,12258,0,0,10` | unknown |
| `AT+CRSM=176,28423,0,0,9` | unknown |
| `AT+CTZU=1` | mutating-or-session-affecting |
| `AT+QCFG=\"ims/ut\"` | unknown |
| `AT+QCFG=\"ims/ut\",1,1,1` | mutating-or-session-affecting |
| `AT+QCFG=\"ims\"` | unknown |
| `AT+QCFG=\"ims\",1` | mutating-or-session-affecting |
| `AT+QLWCFG=\"startup\"` | unknown |
| `AT+QLWCFG=\"startup\",0` | unknown |
| `AT+QNVFR=\"/nv/item_files/modem/mmode/sms_domain_pref\"` | unknown |
| `AT+QNVFW=\"/nv/item_files/modem/mmode/sms_domain_pref\",01` | mutating-or-session-affecting |
| `ATE1` | unknown |
| `ATI5` | read-mostly |
| `AT\$QCPBMPREF=1` | unknown |
| `AT\$QCPBMPREF?` | read-mostly |
| `AT\$QCPDPIMSCFGE=2,1` | unknown |
| `AT\$QCPDPIMSCFGE?` | read-mostly |

#### `…/lockchk.sh`

| AT template | Risk hint |
|---|---|
| `AT+COPS=$LOCK,2,\"$MCC$MNC\"` | mutating-or-session-affecting |
| `AT+COPS=$LOCK,2,\"$MCC$MNC\",0` | mutating-or-session-affecting |
| `AT+COPS=$LOCK,2,\"$MCC$MNC\",12` | mutating-or-session-affecting |
| `AT+COPS=$LOCK,2,\"$MCC$MNC\",13` | mutating-or-session-affecting |
| `AT+COPS=$LOCK,2,\"$MCC$MNC\",2` | mutating-or-session-affecting |
| `AT+COPS=$LOCK,2,\"$MCC$MNC\",7` | mutating-or-session-affecting |
| `AT+COPS=0` | mutating-or-session-affecting |
| `AT+COPS=0,,,0` | mutating-or-session-affecting |
| `AT+COPS=0,,,12` | mutating-or-session-affecting |
| `AT+COPS=0,,,13` | mutating-or-session-affecting |
| `AT+COPS=0,,,2` | mutating-or-session-affecting |
| `AT+COPS=0,,,7` | mutating-or-session-affecting |
| `AT+COPS=3,2;+COPS?` | read-mostly |

#### `…/phone.sh`

| AT template | Risk hint |
|---|---|
| `AT+CNUM` | read-mostly |
| `AT+CPBS=\"ON\";+CPBS?` | read-mostly |
| `AT+CPBW=1,\"$PHONE\",$TON,\"$NAME\"` | unknown |

#### `…/ubloxdata.sh`

| AT template | Risk hint |
|---|---|
| `AT+UCGED=2` | unknown |

#### `…/celldata.sh`

| AT template | Risk hint |
|---|---|
| `AT+COPS=0,0;+COPS? $cps` | mutating-or-session-affecting |

### Connect / LuCI control

#### `…/bandmask`

| AT template | Risk hint |
|---|---|
| `AT!ENTERCND="A710";!BAND?` | read-mostly |
| `AT!ENTERCND="AWRONG"` | unknown |
| `AT#BND=?` | read-mostly |
| `AT#BND?` | read-mostly |
| `AT+CGMM` | read-mostly |
| `AT+GTACT=?` | read-mostly |
| `AT+GTACT?` | read-mostly |
| `AT+QCFG="band"` | unknown |
| `AT+QNWPREFCFG="lte_band"` | mutating-or-session-affecting |
| `AT+QNWPREFCFG="nr5g_band"` | mutating-or-session-affecting |
| `AT+QNWPREFCFG="nsa_nr5g_band"` | mutating-or-session-affecting |
| `AT+XACT?` | read-mostly |
| `AT^SLBAND?` | read-mostly |

#### `…/create_connect.sh`

| AT template | Risk hint |
|---|---|
| `AT+CEREG?;+CREG?` | read-mostly |
| `AT+CFUN=$CFUNOFF` | mutating-or-session-affecting |
| `AT+CFUN=1` | mutating-or-session-affecting |
| `AT+CFUN=1,1` | mutating-or-session-affecting |
| `AT+CGACT=0,$CID` | unknown |
| `AT+CGACT=1,$CID` | unknown |
| `AT+CGATT=0` | unknown |
| `AT+CGCONTRDP=$CID` | unknown |
| `AT+CGDATA=\"M-RAW_IP\",$CID` | unknown |
| `AT+CGDCONT=$CID,\"$IPVAR\",\"$NAPN\"` | mutating-or-session-affecting |
| `AT+CGDCONT=0` | mutating-or-session-affecting |
| `AT+CGDCONT=0,\"IP\"` | mutating-or-session-affecting |
| `AT+CGDCONT=2,\"$IPVAR\",\"ims\"` | mutating-or-session-affecting |
| `AT+CGDCONT=?` | read-mostly |
| `AT+CGDCONT?;+CFUN?` | read-mostly |
| `AT+CGPIAF=1,0,0,0;+XDNS=$CID,1;+XDNS=$CID,2` | unknown |
| `AT+CNMI=0,0,0,0,0` | unknown |
| `AT+CNMI?` | read-mostly |
| `AT+COPS=?` | read-mostly |
| `AT+QCFG="usbnet",1` | mutating-or-session-affecting |
| `AT+QINDCFG=\"all\"` | unknown |
| `AT+QINDCFG=\"all\",0,1` | unknown |
| `AT+QINDCFG=\"all\",1` | unknown |
| `AT+QINDCFG=\"smsincoming\"` | unknown |
| `AT+QINDCFG=\"smsincoming\",0,1` | unknown |
| `AT+XDATACHANNEL=1,1,\"/USBCDC/2\",\"/USBHS/NCM/0\",2,$CID` | unknown |
| `AT^MODE=0` | unknown |
| `AT^USSDMODE=0` | unknown |

#### `…/create_hostless.sh`

| AT template | Risk hint |
|---|---|
| `AT+CEREG?` | read-mostly |
| `AT+CFUN=$CFUNOFF` | mutating-or-session-affecting |
| `AT+CFUN=1` | mutating-or-session-affecting |
| `AT+CGACT=0` | unknown |
| `AT+CGACT=1,1` | unknown |
| `AT+CGATT=1` | unknown |
| `AT+CGAUTH=0,$NAUTH` | unknown |
| `AT+CGAUTH=0,$NAUTH,\"$NUSER\",\"$NPASS\"` | unknown |
| `AT+CGAUTH=1,$NAUTH` | unknown |
| `AT+CGAUTH=1,$NAUTH,\"$NUSER\",\"$NPASS\"` | unknown |
| `AT+CGCONTRDP=1` | unknown |
| `AT+CGDCONT=$CID,\"$IPVAR\",\"$NAPN\",,0,0,1` | mutating-or-session-affecting |
| `AT+CGDCONT=0,\"$IPVAR\",\"$NAPN\"` | mutating-or-session-affecting |
| `AT+CGDCONT=1,"IP","` | mutating-or-session-affecting |
| `AT+CGDCONT=1,"IP","'$NAPN'",,0,0,0,0,0,0,0` | mutating-or-session-affecting |
| `AT+CGDCONT=1,\"$IPVAR\",\"$NAPN\"` | mutating-or-session-affecting |
| `AT+CGDCONT=2,\"$IPVAR\",\"ims\"` | mutating-or-session-affecting |
| `AT+CGDCONT=?` | read-mostly |
| `AT+CGDCONT?;+CFUN?` | read-mostly |
| `AT+CGPADDR=0` | unknown |
| `AT+CGPADDR=1` | unknown |
| `AT+CGPIAF=1,0,0,0` | unknown |
| `AT+CGPIAF=1,0,0,0;+CGDCONT=1` | mutating-or-session-affecting |
| `AT+CGPIAF=1,1,1,0;+CGPADDR` | unknown |
| `AT+CNMI=0,0,0,0,0` | unknown |
| `AT+CNMI?` | read-mostly |
| `AT+COPS=0` | mutating-or-session-affecting |
| `AT+COPS=2;+COPS=3,0` | mutating-or-session-affecting |
| `AT+CREG=0;+CGREG=0;+CEREG=0;+C5GREG=3;+CGEREP=2,1` | unknown |
| `AT+EIAAPN=\"$NAPN\",0,\"$IPVAR\",\"$IPVAR\",$NAUTH,\"$NUSER\",\"$NPASS\"` | unknown |
| `AT+GTRAT=17` | unknown |
| `AT+QICSGP=$CID,$IPN,\"$NAPN\"` | unknown |
| `AT+QINDCFG=\"all\"` | unknown |
| `AT+QINDCFG=\"all\",0,1` | unknown |
| `AT+QINDCFG=\"all\",1` | unknown |
| `AT+QINDCFG=\"smsincoming\"` | unknown |
| `AT+QINDCFG=\"smsincoming\",0,1` | unknown |
| `AT+QMAP=\"WWAN\"` | unknown |
| `AT+QNETDEVCTL=2,$CID,1` | unknown |
| `AT+XACT=4,2` | unknown |
| `AT\$ECMCALL=1` | unknown |

#### `…/disconnect.sh`

| AT template | Risk hint |
|---|---|
| `AT+CFUN=0` | mutating-or-session-affecting |

#### `…/preconnect.sh`

| AT template | Risk hint |
|---|---|
| `AT+GPS=1` | unknown |
| `AT+GPS?` | read-mostly |

#### `…/celltype.sh`

| AT template | Risk hint |
|---|---|
| `AT!SELRAT?` | read-mostly |
| `AT+CGMM` | read-mostly |
| `AT+CNMP?` | read-mostly |
| `AT+GTACT?` | read-mostly |
| `AT+GTRAT?` | read-mostly |
| `AT+MODODR?` | read-mostly |
| `AT+QCFG=\"nwscanmode\"` | unknown |
| `AT+QNWPREFCFG=\"mode_pref\"` | mutating-or-session-affecting |
| `AT+URAT?` | read-mostly |
| `AT+XACT?` | read-mostly |
| `AT+ZSNT?` | read-mostly |
| `AT^QCNCFG?` | read-mostly |
| `AT^SLMODE?` | read-mostly |
| `AT^SYSCFG?` | read-mostly |
| `AT^SYSCFGEX?` | read-mostly |
| `AT^SYSCONFIG?` | read-mostly |

#### `…/lock.sh`

| AT template | Risk hint |
|---|---|
| `AT` | unknown |
| `AT!BAND=00;!BAND=11` | mutating-or-session-affecting |
| `AT!BAND=11,"Test",0,` | mutating-or-session-affecting |
| `AT!BAND=11,1,\"Test\",1,$m64,$m32` | mutating-or-session-affecting |
| `AT!BAND=11,1,\"Test\",3,$m64,$m32` | mutating-or-session-affecting |
| `AT!BAND=11,1,\"Test\",4,$m64,$m32` | mutating-or-session-affecting |
| `AT!BAND=11,\"Test\",0,$mask64,0` | mutating-or-session-affecting |
| `AT!ENTERCND="A710"` | unknown |
| `AT!ENTERCND="AWRONG"` | unknown |
| `AT+` | unknown |
| `AT+""$COMM"="$L1$lte$lst` | unknown |
| `AT+CFUN=1,1` | mutating-or-session-affecting |
| `AT+QCFG="band",0,` | mutating-or-session-affecting |
| `AT+QNWPREFCFG="lte_band",` | mutating-or-session-affecting |
| `AT+QNWPREFCFG="nr5g_band",` | mutating-or-session-affecting |
| `AT+QNWPREFCFG="nsa_nr5g_band",` | mutating-or-session-affecting |
| `AT^BAND_PREF` | unknown |
| `AT^BAND_PREF=LTE,` | unknown |
| `AT^BAND_PREF=LTE,""$lst` | unknown |
| `AT^BAND_PREF=NR5G,` | unknown |
| `AT^BAND_PREF=NR5G,""$lst` | unknown |
| `AT^SLBAND=LTE,2,` | mutating-or-session-affecting |
| `AT^SLBAND=LTE,2,""$lst` | mutating-or-session-affecting |

#### `…/mask.sh`

| AT template | Risk hint |
|---|---|
| `AT+CGMM` | read-mostly |
| `AT+GMR` | unknown |
| `ATI` | read-mostly |

#### `…/modechge.sh`

| AT template | Risk hint |
|---|---|
| `AT!SELRAT=0` | unknown |
| `AT!SELRAT=1` | unknown |
| `AT!SELRAT=2` | unknown |
| `AT!SELRAT=6` | unknown |
| `AT+CFUN=4;+URAT=0;+CFUN=1,1` | mutating-or-session-affecting |
| `AT+CFUN=4;+URAT=2;+CFUN=1,1` | mutating-or-session-affecting |
| `AT+CFUN=4;+URAT=4,0;+CFUN=1,1` | mutating-or-session-affecting |
| `AT+CFUN=4;+URAT=4,2;+CFUN=1,1` | mutating-or-session-affecting |
| `AT+CFUN=4;+URAT=4,3;+CFUN=1,1` | mutating-or-session-affecting |
| `AT+CGMM` | read-mostly |
| `AT+CNMP=109` | unknown |
| `AT+CNMP=13` | unknown |
| `AT+CNMP=14` | unknown |
| `AT+CNMP=2` | unknown |
| `AT+CNMP=38` | unknown |
| `AT+CNMP=71` | unknown |
| `AT+GTACT=10,6,6,` | unknown |
| `AT+GTACT=14,6,6,` | unknown |
| `AT+GTACT=17,6,6,` | unknown |
| `AT+GTACT=2,3,3,` | unknown |
| `AT+GTACT?` | read-mostly |
| `AT+GTRAT=10` | unknown |
| `AT+GTRAT=10,6,6,` | unknown |
| `AT+GTRAT=14` | unknown |
| `AT+GTRAT=17` | unknown |
| `AT+GTRAT=3` | unknown |
| `AT+MODODR=1` | unknown |
| `AT+MODODR=2` | unknown |
| `AT+MODODR=3` | unknown |
| `AT+MODODR=5` | unknown |
| `AT+QCFG=\"nwscanmode\",0` | mutating-or-session-affecting |
| `AT+QCFG=\"nwscanmode\",1` | mutating-or-session-affecting |
| `AT+QCFG=\"nwscanmode\",2` | mutating-or-session-affecting |
| `AT+QCFG=\"nwscanmode\",3` | mutating-or-session-affecting |
| `AT+QNWPREFCFG=\"mode_pref\",AUTO` | mutating-or-session-affecting |
| `AT+QNWPREFCFG=\"mode_pref\",LTE` | mutating-or-session-affecting |
| `AT+QNWPREFCFG=\"mode_pref\",LTE:NR5G` | mutating-or-session-affecting |
| `AT+QNWPREFCFG=\"mode_pref\",NR5G` | mutating-or-session-affecting |
| `AT+QNWPREFCFG=\"mode_pref\",WCDMA` | mutating-or-session-affecting |
| `AT+XACT=1` | unknown |
| `AT+XACT=2` | unknown |
| `AT+XACT=4,1` | unknown |
| `AT+XACT=4,2` | unknown |
| `AT+ZSNT=0,0,0` | unknown |
| `AT+ZSNT=0,0,1` | unknown |
| `AT+ZSNT=0,0,2` | unknown |
| `AT+ZSNT=0,0,6` | unknown |
| `AT+ZSNT=1,0,0` | unknown |
| `AT+ZSNT=2,0,0` | unknown |
| `AT+ZSNT=6,0,0` | unknown |
| `AT^QCNCFG=00` | unknown |
| `AT^QCNCFG=02` | unknown |
| `AT^QCNCFG=03` | unknown |
| `AT^SLMODE=1,4` | unknown |
| `AT^SLMODE=1,6` | unknown |
| `AT^SYSCFG=13,1,40000000,2,4` | unknown |
| `AT^SYSCFG=14,2,40000000,2,4` | unknown |
| `AT^SYSCFG=2,0,40000000,2,4` | unknown |
| `AT^SYSCFG=2,1,40000000,2,4` | unknown |
| `AT^SYSCFG=2,2,40000000,2,4` | unknown |
| `AT^SYSCFGEX=\"00\"` | unknown |
| `AT^SYSCFGEX=\"00\",40000000,2,4,40000000,,` | unknown |
| `AT^SYSCFGEX=\"010203\",40000000,2,4,40000000,,` | unknown |
| `AT^SYSCFGEX=\"01\",40000000,2,4,40000000,,` | unknown |
| `AT^SYSCFGEX=\"020301\",40000000,2,4,40000000,,` | unknown |
| `AT^SYSCFGEX=\"020304\"` | unknown |
| `AT^SYSCFGEX=\"02\"` | unknown |
| `AT^SYSCFGEX=\"02\",40000000,2,4,40000000,,` | unknown |
| `AT^SYSCFGEX=\"030201\",40000000,2,4,40000000,,` | unknown |
| `AT^SYSCFGEX=\"030402\"` | unknown |
| `AT^SYSCFGEX=\"03\"` | unknown |
| `AT^SYSCFGEX=\"03\",40000000,2,4,40000000,,` | unknown |
| `AT^SYSCFGEX=\"0403\"` | unknown |
| `AT^SYSCFGEX=\"04\"` | unknown |

#### `…/protochnge.sh`

| AT template | Risk hint |
|---|---|
| `AT!ENTERCND="A710"` | unknown |
| `AT!ENTERCND="AWRONG"` | unknown |
| `AT!USBCOMP=1,3,1009` | unknown |
| `AT+QCFG="usbnet",0` | mutating-or-session-affecting |
| `AT+QCFG="usbnet",1` | mutating-or-session-affecting |
| `AT+QCFG="usbnet",2` | mutating-or-session-affecting |

#### `…/remodem.sh`

| AT template | Risk hint |
|---|---|
| `AT+CFUN=1,1` | mutating-or-session-affecting |
| `AT+QPOWD=0` | unknown |

#### `…/restartrun.sh`

| AT template | Risk hint |
|---|---|
| `AT+CFUN=1,1` | mutating-or-session-affecting |
| `AT+CFUN=15` | mutating-or-session-affecting |
| `AT+QPOWD=0` | unknown |

#### `…/scancmd.sh`

| AT template | Risk hint |
|---|---|
| `AT!BAND=00;!BAND=11` | mutating-or-session-affecting |
| `AT!BAND=11,"Test",0,` | mutating-or-session-affecting |
| `AT!BAND=11,"Test",0,00000100030818DF,0` | mutating-or-session-affecting |
| `AT!BAND=11,"Test",0,A300BA0E38DF,2,0,0,0` | mutating-or-session-affecting |
| `AT!ENTERCND="A710"` | unknown |
| `AT!ENTERCND="AWRONG"` | unknown |
| `AT!LTEINFO?` | read-mostly |
| `AT+GMR` | unknown |
| `AT+QCFG="band",0,` | mutating-or-session-affecting |
| `AT+QENG="neighbourcell"` | unknown |
| `AT+QNWPREFCFG="lte_band",` | mutating-or-session-affecting |
| `AT+VZWRSRP?` | read-mostly |
| `ATI` | read-mostly |

#### `…/setsim.sh`

| AT template | Risk hint |
|---|---|
| `AT+QUIMSLOT=$snn` | unknown |
| `AT+QUIMSLOT?` | read-mostly |

#### `…/simsel.sh`

| AT template | Risk hint |
|---|---|
| `AT+QUIMSLOT?` | read-mostly |

## How to use this appendix

1. Find a command here.
2. Follow its source/vendor/script provenance.
3. Check the relevant Modem Zoo family page.
4. Confirm model + firmware + current composition.
5. Only then decide whether it is a read-only diagnostic, a semantic action candidate, or a destructive historical recipe.

The purpose is to **preserve knowledge without accidentally turning every known command into an executable preset**.
