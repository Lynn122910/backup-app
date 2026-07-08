#!/bin/bash
# ============================================================
# 一键准备测试环境
#
# VMware/VirtualBox 共享文件夹不支持符号链接/FIFO/设备文件，
# 此脚本会自动将测试文件复制到 /tmp 并创建特殊文件。
#
# 用法:
#   bash setup_special.sh          普通测试（管道+链接）
#   sudo bash setup_special.sh     含设备文件测试
# ============================================================
set -e

SHARED_DIR="$(cd "$(dirname "$0")" && pwd)"
LOCAL_DIR="/tmp/backup-app-test"

echo "共享目录: $SHARED_DIR"

# ─── 检测是否在共享文件系统上 ──────────────────
FS_TYPE=$(df -T "$SHARED_DIR" | tail -1 | awk '{print $2}')
echo "文件系统: $FS_TYPE"

if echo "$FS_TYPE" | grep -qiE 'fuse|vmhgfs|vboxsf|hgfs|9p'; then
    echo ">>> 检测到共享文件系统，复制到本地: $LOCAL_DIR"
    rm -rf "$LOCAL_DIR"
    cp -r "$SHARED_DIR" "$LOCAL_DIR"
    cd "$LOCAL_DIR"
else
    echo ">>> 本地文件系统，直接在当前目录操作"
    cd "$SHARED_DIR"
fi

echo ""
echo "工作目录: $(pwd)"

# ─── 清理旧的特殊文件 ─────────────────────────
rm -f test_backup_source/link_to_readme
rm -f test_backup_source/subdir/link_to_config
rm -f test_backup_source/broken_link
rm -f test_backup_source/deep/a/b/c/link_to_root_readme
rm -f test_backup_source/readme_hardlink
rm -f test_backup_source/config_backup.ini
rm -f test_backup_source/myfifo
rm -f test_backup_source/subdir/data_pipe
rm -f test_backup_source/char_null
rm -f test_backup_source/char_zero
rm -f test_backup_source/char_urandom
rm -f test_backup_source/block_loop0

# ─── 1. 符号链接 ───────────────────────────────
echo ""
echo ">>> 创建符号链接..."

ln -sf readme.txt              test_backup_source/link_to_readme
ln -sf ../config.ini           test_backup_source/subdir/link_to_config
ln -sf /nonexistent/path       test_backup_source/broken_link
ln -sf ../../../../readme.txt  test_backup_source/deep/a/b/c/link_to_root_readme

echo "  ✓ 4 个符号链接"

# ─── 2. 硬链接 ───────────────────────────────
echo ">>> 创建硬链接..."

ln test_backup_source/readme.txt  test_backup_source/readme_hardlink
ln test_backup_source/config.ini  test_backup_source/config_backup.ini

echo "  ✓ 2 个硬链接"

# ─── 3. FIFO 管道 ─────────────────────────────
echo ">>> 创建 FIFO 管道..."

mkfifo test_backup_source/myfifo
mkfifo test_backup_source/subdir/data_pipe

echo "  ✓ 2 个 FIFO"

# ─── 4. 设备文件（仅 root）───────────────────
if [ "$(id -u)" -eq 0 ]; then
    echo ">>> 创建设备文件 (root)..."

    mknod test_backup_source/char_null    c 1 3
    mknod test_backup_source/char_zero    c 1 5
    mknod test_backup_source/char_urandom c 1 9
    mknod test_backup_source/block_loop0  b 7 0

    echo "  ✓ 3 个字符设备 + 1 个块设备"
else
    echo ">>> 跳过设备文件 (需要 root)"
fi

# ─── 5. 结果展示 ─────────────────────────────
echo ""
echo "=============================="
echo "  准备完毕！"
echo "=============================="
echo ""

find test_backup_source -mindepth 1 | sort | while read f; do
    rel="${f#test_backup_source/}"
    if   [ -b "$f" ]; then echo "  [块设备]   $rel"
    elif [ -c "$f" ]; then echo "  [字符设备] $rel"
    elif [ -p "$f" ]; then echo "  [FIFO]     $rel"
    elif [ -L "$f" ]; then echo "  [符号链接] $rel -> $(readlink "$f")"
    elif [ -d "$f" ]; then echo "  [目录]     $rel/"
    elif [ -f "$f" ]; then
        nlink=$(stat -c '%h' "$f")
        [ "$nlink" -gt 1 ] && echo "  [普通文件] $rel  (硬链接 nlink=$nlink)" || echo "  [普通文件] $rel"
    fi
done

echo ""
echo "图形界面中这样填写:"
echo ""
echo "   源目录:   $(pwd)/test_backup_source"
echo "   备份到:   $(pwd)/test_backup_destination"
echo "   恢复到:   $(pwd)/test_restore_target"
echo ""

# ─── 高级元数据提示 ─────────────────────────
echo "------------------------------"
echo "  高级元数据 (ACL/Cap/SELinux)"
echo "------------------------------"
echo ""
echo "如需完整元数据支持，在虚拟机中安装:"
echo "  sudo apt install libacl1-dev libcap-dev libselinux1-dev"
echo ""
echo "然后设置测试 ACL (示例):"
echo "  setfacl -m u:ubuntu:rwx test_backup_source/readme.txt"
echo "  setfacl -m g:ubuntu:r   test_backup_source/config.ini"
echo ""
echo "编译时会自动检测并启用这些功能。"
echo ""
