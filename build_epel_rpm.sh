#!/bin/bash

# check root UID
if ! [ $(id -u) = 0 ]; then
	echo "Please run as root privilege"
	exit 1
fi

# install dependencies
yum -y install rpmdevtools pcre-devel openssl-devel json-c-devel
rpmdev-setuptree

# make and move tar.gz
make dist
mv cryptode*.tar.gz ~/rpmbuild/SOURCES/

# copy extra files
cp platforms/epel/cryptode.spec ~/rpmbuild/SPECS/
cp platforms/epel/cryptoded.service ~/rpmbuild/SOURCES/
cp etc/cryptoded.conf ~/rpmbuild/SOURCES/

# build rpm
cd ~/rpmbuild/
rpmbuild -ba SPECS/cryptode.spec

