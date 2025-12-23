# Quick Start

1. **Start the I/O kernel daemon** (Caladan):
   ```bash
   sudo ./lib/caladan/iokerneld ias nicpci <NIC_PCI_ADDR>
   # Example:
   sudo ./lib/caladan/iokerneld ias nicpci 0000:03:00.0
   ```

2. **Launch the controller**:
   ```bash
   sudo ./build/sandook/controller/controller ./build/sandook/controller/controller.config
   ```

3. **Launch disk server(s)**:
   ```bash
   sudo ./build/sandook/disk_server/disk_server ./build/sandook/disk_server/disk_server_spdk.config <device>
   # Example:
   sudo ./build/sandook/disk_server/disk_server ./build/sandook/disk_server/disk_server_spdk.config nvme0n1
   ```

4. **Launch block device agent**:
   ```bash
   sudo ./build/sandook/blk_dev/blk_dev ./build/sandook/blk_dev/blk_dev.config
   ```

5. **Access storage** via `/dev/ublkb0`

6. **Create a file system**:
    ```bash
    ```

7. **Read/write a file**:
    ```bash
    ```