# Quick Start

## Note
For the following examples, we will use two servers from the testbed.
We assume that you have built Sandook on both of them.

- Host-1 (`zg04.pdos.csail.mit.edu`) will run the controller and a single disk-server.
- Host-2 (`zg03.pdos.csail.mit.edu`) will run the client block device agent and create a file system to access remote storage.

### A Linux File System backed by Sandook

## On Host-1 (`zg04.pdos.csail.mit.edu`)

1. **Start the Caladan I/O kernel daemon** (on a separate terminal):
   ```bash
   sudo ./lib/caladan/iokerneld ias nicpci <NIC_PCI_ADDR>
   # Example:
   sudo ./lib/caladan/iokerneld ias nicpci 0000:03:00.0
   ```

2. **Launch the controller** (on a separate terminal):
   ```bash
   sudo ./build/sandook/controller/controller ./build/sandook/controller/controller.config
   ```

3. **Launch disk server(s)**:
   ```bash
   sudo ./build/sandook/disk_server/disk_server ./build/sandook/disk_server/disk_server_spdk.config <device>
   # Example:
   sudo ./build/sandook/disk_server/disk_server ./build/sandook/disk_server/disk_server_spdk.config nvme0n1
   ```
- Now you may observe some statistics about the newly joined `disk_server` on the `controller` process.

## On Host-2 (`zg03.pdos.csail.mit.edu`)

1. **Start the Caladan I/O kernel daemon** (on a separate terminal):
   ```bash
   sudo ./lib/caladan/iokerneld ias nicpci <NIC_PCI_ADDR>
   # Example:
   sudo ./lib/caladan/iokerneld ias nicpci 0000:03:00.0
   ```

2. **Launch block device agent** (on a separate terminal):
   ```bash
   sudo ./build/sandook/blk_dev/blk_dev ./build/sandook/blk_dev/blk_dev.config
   ```

3. **Verify that the block device is visible in Linux**:
   ```bash
   ls /dev/ublkb0
   ```

4. **Create an Ext-4 file system**:
    ```bash
    sudo mkfs.ext4 /dev/ublkb0
    ```

5. **Verify the file system health**:
   ```bash
   sudo fsck.ext4 /dev/ublkb0
   ```

6. **Mount the file system**:
    ```bash
    sudo mkdir /mnt/sandook
    ```
- After this point, you will start observing activity on the `blk_dev` process because of background file system activities.

7. **Set permissions**:
   ```bash
   sudo chown -R <user:user> /mnt/sandook
   # Example:
   sudo chown -R sandook-user:sandook-user /mnt/sandook
   ```

8. **Create and read/write a test file**:
   ```bash
   cd /mnt/sandook
   # Create a file
   touch hello-world.txt
   # Write to the file
   echo "Hello, World!" > hello-world.txt
   # Ensure the contents are flushed to storage
   sudo sync /mnt/sandook
   sudo tee /proc/sys/vm/drop_caches
   # Read from the file
   cat hello-world.txt
   ```
- You may also download some larger files, do zip/unzip operations etc. and observe the activity on the `disk_server` process running on **Host-1** which will show some IOPS statistics from the disk.

9. **Unmount the file system**:
   ```bash
   cd ~
   sudo umount /mnt/sandook
   ```

10. **Tear-down the block device**:
- Now go back to the terminal where you started `blk_dev` and stop it by pressing `Ctrl-C`.
- Once it exits (ignore if you observe a Segmentation Fault), the `/dev/ublkb0` device should no more be visible.
- On tear-down, it will log how many blocks were read/written.