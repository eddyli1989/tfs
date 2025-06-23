#!/bin/bash

set -e

echo "Installing absolutely essential dependencies only..."

# 安装最小依赖集
sudo dnf install -y cmake make gcc g++ kernel-devel-$(uname -r) git liburing-devel

# 仅配置printk级别
echo "kernel.printk=4" | sudo tee /etc/sysctl.d/99-tfs.conf >/dev/null
sudo sysctl -p /etc/sysctl.d/99-tfs.conf

echo "Minimal dependencies installed. Proceed to build."