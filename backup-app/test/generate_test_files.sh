#!/bin/bash
# ============================================================
# 测试文件生成脚本（精简版）
#
# 生成 ~18 个测试文件，覆盖备份工具的全部筛选/恢复功能
#
# 用法:
#   bash generate_test_files.sh                     → ~/Desktop/backup_test_source
#   bash generate_test_files.sh /path/to/dir         → 指定目录
#   sudo bash generate_test_files.sh                 → 含设备文件
# ============================================================
set -e

# 获取真实用户的家目录（sudo 时 $HOME 会变成 /root）
if [ -n "$SUDO_USER" ]; then
    REAL_HOME=$(eval echo ~"$SUDO_USER")
else
    REAL_HOME="$HOME"
fi

TARGET="${1:-$REAL_HOME/Desktop/backup_test_source}"

echo "========================================="
echo "  备份工具 — 测试文件生成"
echo "========================================="
echo "目标: $TARGET"

rm -rf "$TARGET" 2>/dev/null || sudo rm -rf "$TARGET" 2>/dev/null || true
mkdir -p "$TARGET"
S="$TARGET"

IS_ROOT=0
[ "$(id -u)" -eq 0 ] && IS_ROOT=1

# 辅助
mkf() { mkdir -p "$(dirname "$S/$1")" && dd if=/dev/urandom bs="$2" count=1 of="$S/$1" 2>/dev/null; }
mk0() { touch "$S/$1"; }

echo ""
echo "── 创建测试文件 ──"
echo ""

# ══════════════════════════════════════════════════════════════
# 1. 普通文件 — 不同扩展名 & 大小 & 时间 & 权限
# ══════════════════════════════════════════════════════════════

# ① 标准文本文件
echo "README — 项目说明文档 v1.0" > "$S/readme.txt"
echo "  ✓ readme.txt"

# ② 日志文件 (测试 name_regex='\.log$')
cat > "$S/app.log" <<'EOF'
[2024-07-01 10:00:00] INFO  Application started
[2024-07-01 10:00:01] INFO  Loading configuration
[2024-07-01 10:00:02] INFO  Ready
EOF
echo "  ✓ app.log"

# ③ 配置文件 (测试 name_regex='\.ini$')
printf '[database]\nhost=localhost\nport=3306\n' > "$S/config.ini"
echo "  ✓ config.ini"

# ④ 中型二进制文件 ~100KB (测试 min_size/max_size)
mkf "data.bin" 100000
echo "  ✓ data.bin  (100 KB)"

# ⑤ 大文件 ~2MB (测试 min_size=1000000)
mkf "large_file.bin" 2000000
echo "  ✓ large_file.bin  (2 MB)"

# ⑥ 空文件 0B (测试 max_size=0 排除空文件)
mk0 "empty.dat"
echo "  ✓ empty.dat  (0 B)"

# ⑦-⑨ 不同修改时间 (测试 mtime_after / mtime_before)
echo "Old archive content"  > "$S/old_archive.txt"
touch -t 202501151200.00 "$S/old_archive.txt"
echo "  ✓ old_archive.txt  (2025-01-15)"

echo "Recent content"       > "$S/recent.txt"
# recent = 7 天前 (使用相对日期，适配任何年份)
touch -d "7 days ago" "$S/recent.txt" 2>/dev/null || touch -t 202607011200.00 "$S/recent.txt"
echo "  ✓ recent.txt  (7 天前)"

echo "Today's work"         > "$S/today.txt"
echo "  ✓ today.txt  (当前时间)"

# ⑩ 隐藏文件 (测试 name_regex='^\.' 匹配隐藏文件)
echo "hidden config data"   > "$S/.hidden_config"
echo "  ✓ .hidden_config"

# ⑪ 全大写文件名 (测试大小写敏感匹配)
echo "UPPERCASE"           > "$S/UPPERCASE.TXT"
echo "  ✓ UPPERCASE.TXT"

# ⑫-⑬ 不同权限 (测试元数据恢复)
echo "Read-only content"   > "$S/readonly.txt"
chmod 0444 "$S/readonly.txt"
echo "  ✓ readonly.txt  (0444)"

printf '#!/bin/bash\necho hello\n' > "$S/executable.sh"
chmod 0755 "$S/executable.sh"
echo "  ✓ executable.sh  (0755)"

# ⑭ 扩展属性 (测试 xattr 元数据恢复)
echo "xattr test" > "$S/with_xattr.txt"
if command -v setfattr &>/dev/null; then
    setfattr -n user.comment -v "Backup test" "$S/with_xattr.txt" 2>/dev/null || true
    setfattr -n user.author  -v "Tester"      "$S/with_xattr.txt" 2>/dev/null || true
    echo "  ✓ with_xattr.txt  (+ xattr)"
else
    echo "  ✓ with_xattr.txt  (xattr 跳过: sudo apt install attr)"
fi

# ⑮ ACL (测试 POSIX ACL 元数据恢复)
echo "ACL test" > "$S/with_acl.txt"
if command -v setfacl &>/dev/null; then
    setfacl -m u:${SUDO_USER:-$USER}:rwx "$S/with_acl.txt" 2>/dev/null || true
    echo "  ✓ with_acl.txt  (+ ACL)"
else
    echo "  ✓ with_acl.txt  (ACL 跳过: sudo apt install acl)"
fi

# ⑯ 子目录文件 (测试 path_glob 目录筛选)
mkdir -p "$S/subdir"
echo "Nested file" > "$S/subdir/nested_file.txt"
echo "  ✓ subdir/nested_file.txt"

# ══════════════════════════════════════════════════════════════
# 2. 特殊文件类型 (测试 file_types 筛选)
# ══════════════════════════════════════════════════════════════
echo ""

# ⑰ 符号链接 (测试 file_types=symlink)
ln -sf "readme.txt" "$S/link_to_readme"
echo "  ✓ link_to_readme  (symlink → readme.txt)"

# ⑱ 硬链接 (测试 file_types=hardlink / inode 去重)
ln "$S/readme.txt" "$S/hardlink_readme"
echo "  ✓ hardlink_readme  (hardlink → readme.txt)"

# ⑲ FIFO 命名管道 (测试 file_types=fifo)
mkfifo "$S/myfifo" 2>/dev/null || true
echo "  ✓ myfifo  (FIFO)"

# ⑳ Unix 套接字 (测试 file_types=socket)
if command -v python3 &>/dev/null; then
    python3 -c "
import socket, os
p = os.path.join('$S', 'test_socket')
try: os.unlink(p)
except OSError: pass
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.bind(p)
s.close()
" 2>/dev/null && echo "  ✓ test_socket  (Unix 套接字)" || echo "  ⚠ 套接字创建失败"
fi

# ㉑ 设备文件 (测试 file_types=char_device / block_device, 需 root)
if [ "$IS_ROOT" -eq 1 ]; then
    mknod "$S/char_null" c 1 3 2>/dev/null || true
    mknod "$S/block_loop0" b 7 0 2>/dev/null || true
    echo "  ✓ char_null  (字符设备 /dev/null)"
    echo "  ✓ block_loop0  (块设备 loop0)"
fi

# ══════════════════════════════════════════════════════════════
# 结果
# ══════════════════════════════════════════════════════════════
echo ""
echo "========================================="
echo "  生成完毕"
echo "========================================="
echo ""

COUNT=$(find "$S" \( -type f -o -type l -o -type p -o -type b -o -type c -o -type s \) 2>/dev/null | wc -l)
echo "共 $COUNT 个文件/特殊项"
echo ""

echo "文件清单:"
echo "────────────────────────────────────────────────────────────"
find "$S" -mindepth 1 | sort | while read f; do
    rel="${f#$S/}"
    if   [ -L "$f" ]; then
        printf '  %-35s → %s\n' "$rel" "$(readlink "$f")"
    elif [ -p "$f" ]; then
        printf '  %-35s [FIFO]\n' "$rel"
    elif [ -S "$f" ]; then
        printf '  %-35s [套接字]\n' "$rel"
    elif [ -c "$f" ]; then
        printf '  %-35s [字符设备]\n' "$rel"
    elif [ -b "$f" ]; then
        printf '  %-35s [块设备]\n' "$rel"
    elif [ -d "$f" ]; then
        printf '  %-35s [目录]\n' "$rel"
    elif [ -f "$f" ]; then
        sz=$(stat -c '%s' "$f" 2>/dev/null || echo 0)
        mt=$(stat -c '%Y' "$f" 2>/dev/null || echo 0)
        pm=$(stat -c '%a' "$f" 2>/dev/null || echo "---")
        nl=$(stat -c '%h' "$f" 2>/dev/null || echo 1)
        if [ "$sz" -ge 1000000 ]; then
            sz_str="$(echo "$sz/1000000" | bc -l 2>/dev/null | xargs printf '%.1f MB' || echo "${sz}B")"
        elif [ "$sz" -ge 1000 ]; then
            sz_str="$(echo "$sz/1000" | bc -l 2>/dev/null | xargs printf '%.1f KB' || echo "${sz}B")"
        else
            sz_str="${sz} B"
        fi
        mts=$(date -d @$mt '+%Y-%m-%d' 2>/dev/null || echo "unknown")
        [ "$nl" -gt 1 ] && nl_str=" nlink=$nl" || nl_str=""
        printf '  %-35s %8s  %s  %s%s\n' "$rel" "$sz_str" "$mts" "$pm" "$nl_str"
    else
        printf '  %-35s [其他]\n' "$rel"
    fi
done

echo ""
echo "GUI 中填写: 源目录 = $S"
echo ""
echo "依赖: sudo apt install attr acl"
echo ""
