# Arches Instrumentation — Yocto recipe (REQ §13).
# The recipe builds and installs; all system files (units, sysusers,
# tmpfiles, udev, defaults) ship inside the repo and are installed from it.
SUMMARY = "Arches Instrumentation on-device management and observability suite"
DESCRIPTION = "Hub-and-spoke instrumentation: istate hub, nine spokes, one external face"
HOMEPAGE = "https://arches.example"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=3b83ef96387f14655fc854ddc3c6bd57"

SRC_URI = "git://git.arches.example/instrumentation.git;protocol=https;branch=main"
SRCREV = "${AUTOREV}"
PV = "1.0.0"
S = "${WORKDIR}/git"

inherit cmake systemd pkgconfig

DEPENDS = "grpc protobuf grpc protobuf-native grpc-native yaml-cpp gtest-native"
RDEPENDS:${PN} = "wireguard-tools chrony ptp4l gpsd swupdate"

EXTRA_OECMAKE = " \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF \
    -DCMAKE_SYSTEM_PROCESSOR=${TARGET_ARCH} \
"

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/daemons/istate/istate        ${D}${bindir}/instr-istate
    install -m 0755 ${B}/daemons/networkd/networkd    ${D}${bindir}/instr-networkd
    install -m 0755 ${B}/daemons/monitord/monitord    ${D}${bindir}/instr-monitord
    install -m 0755 ${B}/daemons/hardware/hardware    ${D}${bindir}/instr-hardware
    install -m 0755 ${B}/daemons/faults/faults        ${D}${bindir}/instr-faults
    install -m 0755 ${B}/daemons/alarms/alarms        ${D}${bindir}/instr-alarms
    install -m 0755 ${B}/daemons/logmanager/logmanager ${D}${bindir}/instr-logmanager
    install -m 0755 ${B}/daemons/telemetry/telemetry  ${D}${bindir}/instr-telemetry
    install -m 0755 ${B}/daemons/ota/ota              ${D}${bindir}/instr-ota
    install -m 0755 ${B}/daemons/backup/backup        ${D}${bindir}/instr-backup
    install -m 0755 ${B}/tools/istate_ctrl/istate_ctrl      ${D}${bindir}/istate_ctrl
    install -m 0755 ${B}/tools/network_ctrl/network_ctrl    ${D}${bindir}/network_ctrl
    install -m 0755 ${B}/tools/monitor_ctrl/monitor_ctrl    ${D}${bindir}/monitor_ctrl
    install -m 0755 ${B}/tools/hardware_ctrl/hardware_ctrl  ${D}${bindir}/hardware_ctrl
    install -m 0755 ${B}/tools/fault_ctrl/fault_ctrl        ${D}${bindir}/fault_ctrl
    install -m 0755 ${B}/tools/alarm_ctrl/alarm_ctrl        ${D}${bindir}/alarm_ctrl
    install -m 0755 ${B}/tools/log_ctrl/log_ctrl            ${D}${bindir}/log_ctrl
    install -m 0755 ${B}/tools/telemetry_ctrl/telemetry_ctrl ${D}${bindir}/telemetry_ctrl
    install -m 0755 ${B}/tools/ota_ctrl/ota_ctrl            ${D}${bindir}/ota_ctrl
    install -m 0755 ${B}/tools/backup_ctrl/backup_ctrl      ${D}${bindir}/backup_ctrl

    install -d ${D}${systemd_unitdir}/system
    install -m 0644 ${S}/system/systemd/*.service ${D}${systemd_unitdir}/system/
    install -d ${D}${sysconfdir}/sysusers.d
    install -m 0644 ${S}/system/sysusers.d/instrumentation.conf ${D}${sysusers.d}/
    install -d ${D}${nonarch_libdir}/tmpfiles.d
    install -m 0644 ${S}/system/tmpfiles.d/instrumentation.conf ${D}${nonarch_libdir}/tmpfiles.d/
    install -d ${D}${sysconfdir}/udev/rules.d
    install -m 0644 ${S}/system/udev/70-instrumentation.rules ${D}${sysconfdir}/udev/rules.d/
    install -d ${D}${sysconfdir}/instrumentation
    install -m 0644 ${S}/descriptors/devices-*.yaml ${D}${sysconfdir}/instrumentation/
    install -m 0644 ${S}/descriptors/capabilities-*.yaml ${D}${sysconfdir}/instrumentation/
    install -m 0644 ${S}/descriptors/resource-profile.yaml ${D}${sysconfdir}/instrumentation/
    install -d ${D}${sysconfdir}/instrumentation/catalog
    install -m 0644 ${S}/catalog/alarms.yaml ${D}${sysconfdir}/instrumentation/catalog/
    install -m 0644 ${S}/catalog/fault-rules.yaml ${D}${sysconfdir}/instrumentation/
    install -d ${D}${datadir}/instrumentation/defaults
    for d in ${S}/system/defaults/*/; do
        install -d ${D}${datadir}/instrumentation/defaults/$(basename ${d})
        install -m 0644 ${d}/config.yaml ${D}${datadir}/instrumentation/defaults/$(basename ${d})/
    done
    install -d ${D}${datadir}/instrumentation/schemas
    install -m 0644 ${S}/schemas/*.json ${D}${datadir}/instrumentation/schemas/
}

SYSTEMD_PACKAGES = "${PN}"
SYSTEMD_SERVICE:${PN} = "instr-istate.service instr-networkd.service instr-monitord.service instr-hardware.service instr-faults.service instr-alarms.service instr-logmanager.service instr-telemetry.service instr-ota.service instr-backup.service"
SYSTEMD_AUTO_ENABLE = "enable"

FILES:${PN} += "${sysusersdir} ${nonarch_libdir}/tmpfiles.d ${datadir}/instrumentation"
