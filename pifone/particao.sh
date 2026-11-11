#!/bin/sh
# PiFone: na primeira vez que liga, usa todo o espaco livre do cartao para
# criar a unidade MUSICAS (FAT32), que aparece em qualquer computador.
MNT=/srv/musicas

ROOT=$(findmnt -no SOURCE /)
DISK=/dev/$(lsblk -no PKNAME "$ROOT")
case "$DISK" in
    *[0-9]) PART="${DISK}p3" ;;
    *)      PART="${DISK}3" ;;
esac

if [ ! -b "$PART" ]; then
    echo "Criando a particao de musicas em $DISK"
    END=$(parted -sm "$DISK" unit s print | awk -F: '$1=="2"{gsub("s","",$3); print $3}')
    [ -n "$END" ] || { echo "Nao achei o fim da particao 2"; exit 1; }
    START=$(( (END / 8192 + 1) * 8192 ))
    parted -s "$DISK" -- mkpart primary fat32 "${START}s" 100% || exit 1
    partprobe "$DISK" 2>/dev/null || true
    udevadm settle
    sleep 2
fi

if [ -b "$PART" ] && [ -z "$(blkid -o value -s TYPE "$PART")" ]; then
    echo "Formatando $PART como MUSICAS"
    mkfs.vfat -F 32 -n MUSICAS "$PART" || exit 1
    udevadm settle
    sleep 1
fi

mountpoint -q "$MNT" || mount "$MNT" || mount -o ro "$PART" "$MNT"
exit 0
