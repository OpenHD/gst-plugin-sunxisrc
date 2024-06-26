#!/bin/bash
# This file is the install instruction for the CHROOT build
# We're using cloudsmith-cli to upload the file in CHROOT
sudo apt install -y python3-pip
sudo pip3 install --upgrade cloudsmith-cli
curl -1sLf 'https://dl.cloudsmith.io/public/openhd/release/setup.deb.sh'| sudo -E bash
apt update
sudo apt install -y git automake ruby-dev curl make cmake gcc g++ wget libdrm-dev mlocate openhd qopenhd-rk3566 apt-transport-https apt-utils open-hd-web-ui
gem install fpm
sudo ./configure
sudo make 
sudo mkdir -p /opt/sunxi
sudo make install DESTDIR=/opt/sunxi
VERSION="0.3-$(date +'%m/%d/%Y')"
VERSION=$(echo "$VERSION" | sed 's/\//-/g')
mkdir -p /opt/sunxi/usr/lib/arm-linux-gnueabihf/gstreamer-1.0/
mv /opt/sunxi/usr/local/lib/gstreamer-1.0/* /opt/sunxi/usr/lib/arm-linux-gnueabihf/gstreamer-1.0/
rm -Rf /opt/sunxi/usr/local/
fpm -a armhf -s dir -t deb -n encode-sunxi -v "$VERSION" -C /opt/sunxi -p encode-sunxi_VERSION_ARCH.deb
echo "push to cloudsmith"
git describe --exact-match HEAD >/dev/null 2>&1
echo "Pushing the package to OpenHD 2.3 repository"
API_KEY=$(cat /opt/additionalFiles/cloudsmith_api_key.txt)

cloudsmith push deb --api-key "$API_KEY" openhd/dev-release/debian/bullseye *.deb || exit 1
