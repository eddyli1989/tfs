# 创建测试挂载点
sudo mkdir -p /mnt/tfs_test
sudo mount -t tfs none /mnt/tfs_test

# 简单文件写入测试
echo "Hello TFS" | sudo tee /mnt/tfs_test/testfile.txt

# 检查用户态程序日志
sudo ./build/tfsd/tfsd &  # 启动守护进程
sleep 1
sudo pkill tfsd  # 停止守护进程

# 查看内核日志
dmesg | grep TFS | tail -n 20

# 卸载文件系统
sudo umount /mnt/tfs_test
sudo rmmod tfs_client