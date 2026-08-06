#!/bin/sh

# Shared, conservative USB gadget transitions for the HiBy R1.
# The R1 has one USB device controller, so ADB and mass storage cannot own it
# at the same time. Keep transitions serialized and never expose a mounted SD
# block device to the host.

USB_GADGET_ROOT=/sys/kernel/config/usb_gadget
USB_MASS_GADGET=$USB_GADGET_ROOT/android0
USB_ADB_GADGET=$USB_GADGET_ROOT/adb_demo
USB_SD_BLOCK=/dev/mmcblk0
USB_SD_PART=/dev/mmcblk0p1
USB_SD_MOUNT=/usr/data/mnt/sd_0
USB_MODE_LOCK=/tmp/r1-usb-mode.lock
USB_MODE_LOG=/tmp/r1-usb-mode.log

usb_log() {
    echo "$(date '+%Y-%m-%d %H:%M:%S' 2>/dev/null) $*" >>$USB_MODE_LOG
}

usb_lock() {
    tries=0
    while ! mkdir $USB_MODE_LOCK 2>/dev/null; do
        owner=$(cat $USB_MODE_LOCK/pid 2>/dev/null)
        if [ -z "$owner" ] || ! kill -0 "$owner" 2>/dev/null; then
            rm -f $USB_MODE_LOCK/pid 2>/dev/null || true
            rmdir $USB_MODE_LOCK 2>/dev/null || true
            continue
        fi
        tries=$((tries + 1))
        [ $tries -ge 5 ] && return 1
        sleep 1
    done
    echo $$ >$USB_MODE_LOCK/pid
    return 0
}

usb_unlock() {
    rm -f $USB_MODE_LOCK/pid 2>/dev/null || true
    rmdir $USB_MODE_LOCK 2>/dev/null || true
}

usb_gadget_bound() {
    [ -n "$(cat "$1/UDC" 2>/dev/null)" ]
}

usb_sd_is_mounted() {
    grep -q " $USB_SD_MOUNT " /proc/mounts 2>/dev/null
}

usb_mount_sd_local() {
    [ -b $USB_SD_PART ] || return 0
    usb_sd_is_mounted && return 0

    mkdir -p $USB_SD_MOUNT || return 1
    if mount -t exfat -o rw $USB_SD_PART $USB_SD_MOUNT 2>>$USB_MODE_LOG; then
        usb_log "mounted SD locally as exFAT"
        return 0
    fi
    if mount -t vfat -o rw,utf8 $USB_SD_PART $USB_SD_MOUNT 2>>$USB_MODE_LOG; then
        usb_log "mounted SD locally as FAT"
        return 0
    fi
    usb_log "failed to mount $USB_SD_PART locally"
    return 1
}

usb_unmount_sd_local() {
    usb_sd_is_mounted || return 0
    sync
    if umount $USB_SD_MOUNT 2>>$USB_MODE_LOG; then
        usb_log "unmounted SD before mass-storage export"
        return 0
    fi
    usb_log "refused mass-storage export: SD is busy"
    return 1
}

usb_remove_mass_storage_gadget() {
    [ -d $USB_MASS_GADGET ] || return 0

    echo "" >$USB_MASS_GADGET/UDC 2>/dev/null || true
    for lun in $USB_MASS_GADGET/functions/mass_storage.*/lun.*/file; do
        [ -e "$lun" ] || continue
        echo "" >"$lun" 2>/dev/null || true
    done
    for link in $USB_MASS_GADGET/configs/*/mass_storage.*; do
        [ -e "$link" ] || [ -L "$link" ] || continue
        rm "$link" 2>/dev/null || true
    done
    for function in $USB_MASS_GADGET/functions/mass_storage.*; do
        [ -d "$function" ] || continue
        rmdir "$function" 2>/dev/null || true
    done
    for strings in $USB_MASS_GADGET/configs/*/strings/*; do
        [ -d "$strings" ] || continue
        rmdir "$strings" 2>/dev/null || true
    done
    for config in $USB_MASS_GADGET/configs/*; do
        [ -d "$config" ] || continue
        rmdir "$config" 2>/dev/null || true
    done
    for strings in $USB_MASS_GADGET/strings/*; do
        [ -d "$strings" ] || continue
        rmdir "$strings" 2>/dev/null || true
    done
    rmdir $USB_MASS_GADGET 2>/dev/null || true

    if [ -d $USB_MASS_GADGET ]; then
        usb_log "mass-storage gadget cleanup incomplete"
        return 1
    fi
    usb_log "mass-storage gadget released"
    return 0
}

usb_stop_adb_gadget() {
    if [ -d $USB_ADB_GADGET ]; then
        echo "" >$USB_ADB_GADGET/UDC 2>/dev/null || true
    fi
    killall adbserver.sh >/dev/null 2>&1 || true
    killall adbd >/dev/null 2>&1 || true
    sleep 1
    # The stock adbserver.sh loops forever and can race a graceful signal by
    # spawning a replacement adbd. Ensure both processes are gone before the
    # detached transition worker builds the next gadget.
    killall -9 adbserver.sh >/dev/null 2>&1 || true
    killall -9 adbd >/dev/null 2>&1 || true
    umount /dev/usb-ffs/adb 2>/dev/null || true

    if [ -d $USB_ADB_GADGET ]; then
        rm $USB_ADB_GADGET/configs/c.1/ffs.adb 2>/dev/null || true
        rmdir $USB_ADB_GADGET/functions/ffs.adb 2>/dev/null || true
        rmdir $USB_ADB_GADGET/configs/c.1/strings/0x409 2>/dev/null || true
        rmdir $USB_ADB_GADGET/configs/c.1 2>/dev/null || true
        rmdir $USB_ADB_GADGET/strings/0x409 2>/dev/null || true
        rmdir $USB_ADB_GADGET 2>/dev/null || true
    fi
    rmdir /dev/usb-ffs/adb 2>/dev/null || true
    rmdir /dev/usb-ffs 2>/dev/null || true

    if [ -d $USB_ADB_GADGET ]; then
        usb_log "ADB gadget cleanup incomplete"
        return 1
    fi
    usb_log "ADB gadget stopped"
    return 0
}

usb_unmount_empty_configfs() {
    [ -d $USB_GADGET_ROOT ] || return 0
    for gadget in $USB_GADGET_ROOT/*; do
        [ -d "$gadget" ] && return 0
    done
    umount /sys/kernel/config 2>/dev/null || true
}

usb_adb_ready() {
    usb_gadget_bound $USB_ADB_GADGET && pidof adbd >/dev/null 2>&1
}

usb_adb_serial() {
    serial=dev
    if [ "${env_adb_device_use_diffrent_name:-n}" = "y" ]; then
        if [ -f /sys/class/net/wlan0/address ]; then
            serial=$(cat /sys/class/net/wlan0/address 2>/dev/null | sed 's/[^0-9a-zA-Z]//g')
            serial=${serial#????????}
        elif [ -f /sys/class/misc/efuse-string-version/dev ]; then
            serial=$(cmd_efuse read CHIP_ID 2>/dev/null | sed 's/[^0-9a-zA-Z]//g')
            serial=$(echo "$serial" | cut -c1-4)
        fi
    fi
    [ -n "$serial" ] || serial=dev
    echo "${env_adb_device_name_prefix:-ingenic}_$serial"
}

usb_start_adb_direct() {
    [ ! -f /usr/data/disableadb ] || {
        usb_log "ADB start blocked by /usr/data/disableadb"
        return 1
    }

    if [ ! -d $USB_GADGET_ROOT ]; then
        mount -t configfs none /sys/kernel/config >>$USB_MODE_LOG 2>&1 || return 1
    fi
    mkdir $USB_ADB_GADGET 2>>$USB_MODE_LOG || return 1
    cd $USB_ADB_GADGET || return 1

    echo 0x18d1 >idVendor || return 1
    echo 0xd002 >idProduct || return 1
    echo 0x200 >bcdUSB || return 1
    echo 0x100 >bcdDevice || return 1

    mkdir strings/0x409 || return 1
    echo ingenic >strings/0x409/manufacturer
    echo composite-adb >strings/0x409/product
    usb_adb_serial >strings/0x409/serialnumber

    mkdir configs/c.1 || return 1
    echo 120 >configs/c.1/MaxPower
    mkdir configs/c.1/strings/0x409 || return 1
    echo adb >configs/c.1/strings/0x409/configuration

    mkdir functions/ffs.adb || return 1
    ln -s functions/ffs.adb configs/c.1/ffs.adb || return 1
    mkdir -p /dev/usb-ffs/adb || return 1
    cd / || return 1

    if [ -x /sbin/adbserver.sh ]; then
        /sbin/adbserver.sh 440 >>$USB_MODE_LOG 2>&1 &
    else
        /usr/bin/adbd >>$USB_MODE_LOG 2>&1 &
    fi
    usb_log "ADB gadget direct fallback started"
    return 0
}

usb_start_adb() {
    if usb_adb_ready; then
        # A previous interrupted transition may leave an unbound android0 LUN
        # holding the SD block device even though ADB is healthy.
        usb_remove_mass_storage_gadget || return 1
        usb_mount_sd_local
        return $?
    fi

    usb_stop_adb_gadget || return 1
    usb_remove_mass_storage_gadget || return 1
    usb_unmount_empty_configfs

    # ADB must leave the SD mounted locally for audiobook and music access.
    usb_mount_sd_local || return 1

    if ! /etc/init.d/adb/S440adb start >>$USB_MODE_LOG 2>&1; then
        usb_log "stock ADB start failed; using direct configfs fallback"
        usb_stop_adb_gadget || return 1
        usb_start_adb_direct || return 1
    fi
    tries=0
    while [ $tries -lt 5 ]; do
        usb_adb_ready && {
            usb_log "ADB gadget started"
            return 0
        }
        tries=$((tries + 1))
        sleep 1
    done
    usb_log "ADB gadget failed to start"
    return 1
}

usb_start_mass_storage() {
    # Prefer unmounting while ADB is still available. Some stock-started adbd
    # processes inherit an SD database descriptor, however, so a busy result
    # gets one retry after stopping ADB. If another process still owns the
    # filesystem, restore ADB and never expose the mounted block device.
    first_unmount_ok=0
    if usb_unmount_sd_local; then
        first_unmount_ok=1
    else
        usb_log "initial SD unmount busy; retrying after ADB shutdown"
    fi

    if ! usb_stop_adb_gadget; then
        usb_mount_sd_local || true
        return 1
    fi

    if [ $first_unmount_ok -ne 1 ] && ! usb_unmount_sd_local; then
        usb_log "mass-storage export cancelled; restoring ADB"
        usb_start_adb || true
        return 1
    fi

    usb_remove_mass_storage_gadget || return 1
    usb_unmount_empty_configfs

    if /usr/bin/usb_dev_mass_storage.sh start $USB_SD_BLOCK >>$USB_MODE_LOG 2>&1; then
        usb_log "mass-storage gadget started"
        return 0
    fi

    usb_log "mass-storage start failed; restoring local SD and ADB"
    usb_remove_mass_storage_gadget || true
    usb_unmount_empty_configfs
    usb_mount_sd_local || true
    /etc/init.d/adb/S440adb start >>$USB_MODE_LOG 2>&1 || true
    return 1
}
