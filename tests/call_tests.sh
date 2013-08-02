#!/bin/bash +x

TESTS_DIR="/tmp/hepunion_tests"

# This is for root, only
if [ $EUID -ne 0 ]; then
	echo "Please, sudo make tests"
	exit 1
fi

# Delete previous tree
rm -rf $TESTS_DIR

# Recreate tree
mkdir -p $TESTS_DIR
pushd $TESTS_DIR

mkdir root
mkdir snapshot

pushd root
mkdir ro_dir
touch ro_file
touch ro_dir/ro_file
popd

pushd snapshot
mkdir rw_dir
touch rw_file
touch rw_dir/rw_file
popd

mkdir export
popd

# Remove any running instance
rmmod hepunion
# Load module
insmod ../hepunion.ko

# Mount
mount -t HEPunion -o $TESTS_DIR/snapshot/=RW:$TESTS_DIR/root/=RO none $TESTS_DIR/export/

./hepunion_tests $TESTS_DIR

# Unmount
umount $TESTS_DIR/export/

# Remove module
rmmod hepunion
