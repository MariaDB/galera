#!/bin/bash -eu

if [ $# -ne 1 ]
then
    echo "Usage: $0 <version>"
    exit 1
fi

set -x

# Absolute path of this script folder
SCRIPT_ROOT=$(cd $(dirname $0); pwd -P)
THIS_DIR=$(pwd -P)

RPM_TOP_DIR=$SCRIPT_ROOT/rpm_top_dir
rm -rf $RPM_TOP_DIR
mkdir -p $RPM_TOP_DIR/RPMS
ln -s ../../../ $RPM_TOP_DIR/BUILD

fast_cflags="-O3 -fno-omit-frame-pointer"
uname -m | grep -q i686 && \
cpu_cflags="-mtune=i686" || cpu_cflags="-mtune=core2"
RPM_OPT_FLAGS="$fast_cflags $cpu_cflags"
GALERA_SPEC=$SCRIPT_ROOT/galera.spec

RELEASE=${RELEASE:-"1"}

if  [ -r /etc/fedora-release ]
then
    DISTRO_VERSION=fc$(rpm -qf --qf '%{version}\n' /etc/fedora-release)
elif [ -r /etc/redhat-release ]
then
    DISTRO_VERSION=rhel$(rpm -qf --qf '%{version}\n' /etc/redhat-release)
elif [ -r /etc/SuSE-release ]
then
    DISTRO_VERSION=sles$(rpm -qf --qf '%{version}\n' /etc/SuSE-release | cut -d. -f1)
else
    DISTRO_VERSION=
fi

# Perhaps not needed for fc20? As it causes package name to be
# galera...fc20.fc20..rpm
# See %_rpmfilename (http://rpm.org/api/4.4.2.2/config_macros.html)
[ -n "$DISTRO_VERSION" ] && RELEASE=$RELEASE.$DISTRO_VERSION

# %dist does not return a value for centos5
# https://bugs.centos.org/view.php?id=3239
if [ ${DISTRO_VERSION} = "rhel5" ]
then
  DIST_TAG="dist .el5"
else
  DIST_TAG="_foo bar"
fi

$(which rpmbuild) --clean --define "_topdir $RPM_TOP_DIR" \
                  --define "optflags $RPM_OPT_FLAGS" \
                  --define "version $1" \
                  --define "release $RELEASE" \
                  --define "$DIST_TAG" \
                  -bb $GALERA_SPEC

RPM_ARCH=$(rpm --showrc | grep "^build arch" | awk '{print $4}')

mv $RPM_TOP_DIR/RPMS/$RPM_ARCH/galera-*.rpm ./

rm -rf $RPM_TOP_DIR

exit 0

