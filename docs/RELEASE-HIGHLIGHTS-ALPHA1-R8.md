# MedveModem 0.16.0 Alpha 1 r8 — Release Highlights

## 0.16.0-alpha1-r8

- **Quectel EP06-E/EG06 LTE cell locking:** introduce an EP06-specific radio dialect that probes both `AT+QNWLOCK="common/4g"` and `AT+QNWLOCK="common/lte"`, validates the actual firmware response, then offers LTE EARFCN+PCI lock/unlock from LuCI. The two command formats have different lock-mode numbers; they are never used interchangeably.
- **Safer rollback:** record the original writable QNWLOCK setting (including complete multi-cell lists) before modifying the lock; the read-only /lte completion-status field is not replayed in a rollback write. Refuse changes if the query is unsupported, malformed, pending or ambiguous. The existing radio transaction journal, connectivity guard and rollback engine remain in charge.
- **Native EP06 cell discovery:** read serving LTE and neighbour LTE cells with `AT+QENG`, parse PCI, EARFCN, RSRP, RSRQ, RSSI, SINR and LTE band, and offer a Lock action beside the detected cells. Do not duplicate a serving cell in the neighbour list; interpret QENG SINR as dB and neighbour RSRQ before RSRP.
- **Temperature and aggregation:** query `AT+QTEMP` alongside `AT+QCAINFO` on the engine-owned AT port. Ignore missing/invalid sensor values, report maximum valid temperature and retain correct PCC/SCC active/inactive semantics.
- **Ownership protection:** working ModemManager control remains the default for EP06-E; native QMI must be selected manually. No direct AT traffic while ModemManager or QModem owns the modem. LuCI explains the limitation, shows EP06 cell-lock-only controls and preserves ModemManager network-mode editing.
- **Release discipline:** align package and browser revisions to r8, add EP06 parser fixture tests, and make all OpenWrt SDK jobs manual-only. No push or pull request starts a package build; publication is an explicit opt-in after the full six-target matrix and checksum verification.
- **Installer:** includes the post-r7 fix for BusyBox systems lacking `paste`, with regression coverage for GitHub release selection.

**Validation:** host parser fixtures and frontend/Chromium gates run before the six SDK builds. The exact EP06-E firmware and RF environment still require live router acceptance; code-level protection is not a substitute for hardware testing.

Release: https://github.com/Medvedolog/medvemodem/releases/tag/v0.16.0-alpha1-r8

## Post-release correction notice (source-only; not in published r8)

A field report after deploying r8 found that choosing MedveModem native QMI for EP06-E could appear ineffective: Save did not generate a bottom toast, queued `bringup` did not report the final managing service, and EP06 AT cell polling compared against the nonexistent `owner.control="native"` rather than `netifd-qmi`. The `main` branch now includes explicit owner-change statuses, UCI save verification, real owner/connection confirmation, live QMI/MBIM control-node choice and corrected EP06 native cell polling. These corrections were **not** included in the original published r8 archives. A subsequent versioned release and router acceptance are required. See `docs/HANDOFF-ALPHA1-R8.md`.
