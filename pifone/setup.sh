#!/bin/bash
# Roda DENTRO da imagem (chroot) durante a montagem no GitHub Actions.
set -ex
export DEBIAN_FRONTEND=noninteractive
SRC=/tmp/pifone
MUSICAS=/srv/musicas
BOOT=/boot/firmware

# Nao deixar o apt tentar iniciar servicos dentro do chroot
printf '#!/bin/sh\nexit 101\n' > /usr/sbin/policy-rc.d
chmod +x /usr/sbin/policy-rc.d

apt-get update
apt-get install -y --no-install-recommends \
    bluez bluez-tools bluez-alsa-utils libasound2-plugin-bluez \
    mpd mpc python3-evdev rfkill parted dosfstools
apt-get clean
rm -rf /var/lib/apt/lists/*

# ---- usuario para manutencao (teclado + HDMI): pi / pifone
id pi >/dev/null 2>&1 || useradd -m -s /bin/bash -G sudo,audio pi
echo 'pi:pifone' | chpasswd
systemctl disable userconfig.service 2>/dev/null || true
echo pifone > /etc/hostname
sed -i 's/127\.0\.1\.1.*/127.0.1.1\tpifone/' /etc/hosts

# ---- pasta de musicas = particao MUSICAS (criada na primeira vez que liga)
mkdir -p "$MUSICAS"
usermod -aG audio mpd
MPD_UID=$(id -u mpd)
AUDIO_GID=$(getent group audio | cut -d: -f3)
echo "LABEL=MUSICAS $MUSICAS vfat ro,nofail,noatime,uid=$MPD_UID,gid=$AUDIO_GID,umask=002,utf8,x-systemd.device-timeout=5s 0 0" >> /etc/fstab

# ---- tocador
cat > /etc/mpd.conf <<EOF
music_directory     "$MUSICAS"
playlist_directory  "/var/lib/mpd/playlists"
db_file             "/var/lib/mpd/tag_cache"
state_file          "/var/lib/mpd/state"
state_file_interval "15"
user                "mpd"
bind_to_address     "127.0.0.1"
port                "6600"
auto_update         "no"
restore_paused      "yes"

audio_output {
    type        "alsa"
    name        "Fone Bluetooth"
    device      "bluealsa"
    mixer_type  "software"
}
EOF
systemctl enable mpd.service
systemctl disable mpd.socket 2>/dev/null || true

# ---- botoes do fone chegam pelo uinput
echo uinput > /etc/modules-load.d/uinput.conf

# ---- programas do PiFone
install -m 755 "$SRC/pifone.py"   /usr/local/bin/pifone
install -m 755 "$SRC/particao.sh" /usr/local/bin/pifone-particao
install -m 644 "$SRC/pifone.txt"  "$BOOT/pifone.txt"

cat > /etc/systemd/system/pifone-particao.service <<'EOF'
[Unit]
Description=PiFone - unidade MUSICAS
After=local-fs.target systemd-udev-settle.service
Before=mpd.service pifone.service

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/usr/local/bin/pifone-particao

[Install]
WantedBy=multi-user.target
EOF

cat > /etc/systemd/system/pifone-agent.service <<'EOF'
[Unit]
Description=PiFone - agente de pareamento Bluetooth
After=bluetooth.service
Requires=bluetooth.service

[Service]
ExecStart=/usr/bin/bt-agent -c NoInputNoOutput
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

cat > /etc/systemd/system/pifone.service <<'EOF'
[Unit]
Description=PiFone - tocador de musica para fone Bluetooth
After=bluetooth.service bluealsa.service bluez-alsa.service mpd.service pifone-agent.service pifone-particao.service
Wants=bluetooth.service mpd.service

[Service]
# Guarda o registro da ultima vez que ligou na particao de boot, para dar
# para ler no computador se algo nao funcionar.
ExecStartPre=-/bin/sh -c 'journalctl -u pifone -u pifone-particao -u bluetooth -u mpd -b -1 -n 300 --no-pager > /boot/firmware/pifone-log.txt 2>&1'
ExecStart=/usr/local/bin/pifone
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

ls /lib/systemd/system /usr/lib/systemd/system 2>/dev/null | grep -i -E 'blue|alsa' | sort -u
for s in bluealsa.service bluez-alsa.service; do
    [ -e "/lib/systemd/system/$s" ] && systemctl enable "$s"
done
systemctl disable bluealsa-aplay.service triggerhappy.service 2>/dev/null || true
systemctl enable bluetooth.service pifone-particao.service pifone-agent.service pifone.service

# ---- o cartao nao deve ser todo ocupado pelo sistema: sobra vira MUSICAS
systemctl disable resize2fs_once.service 2>/dev/null || true
echo "cmdline antes: $(cat $BOOT/cmdline.txt)"
sed -i -E 's# init=[^ ]+##; s# resize( |$)#\1#' "$BOOT/cmdline.txt"
echo "cmdline depois: $(cat $BOOT/cmdline.txt)"

# ---- so Bluetooth: Wi-Fi desligado (menos consumo, audio mais estavel)
printf '\n[all]\n# PiFone\ndtoverlay=disable-wifi\n' >> "$BOOT/config.txt"

# ---- liga mais rapido
systemctl disable NetworkManager-wait-online.service 2>/dev/null || true
systemctl disable apt-daily.timer apt-daily-upgrade.timer man-db.timer 2>/dev/null || true

rm -f /usr/sbin/policy-rc.d
rm -rf "$SRC"
