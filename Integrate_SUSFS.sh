KERNEL_ROOT=$(pwd)
git clone https://gitlab.com/simonpunk/susfs4ksu
cd susfs4ksu
git checkout kernel-4.19
cp ./kernel_patches/KernelSU/10_enable_susfs_for_ksu.patch $KERNEL_ROOT/KernelSU/
cp ./kernel_patches/50_add_susfs_in_kernel-4.19.patch $KERNEL_ROOT/
cp ./kernel_patches/fs/* $KERNEL_ROOT/fs/
cp ./kernel_patches/include/linux/* $KERNEL_ROOT/include/linux/
cd $KERNEL_ROOT/KernelSU
patch -p1 < 10_enable_susfs_for_ksu.patch
cd $KERNEL_ROOT
patch -p1 < 50_add_susfs_in_kernel
