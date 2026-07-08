#!/bin/bash
# ============================================================
# 元数据恢复验证脚本
#
# 用法:
#   bash verify_metadata.sh <源目录> <恢复目录>
#
# 在 GUI 中备份源目录 → 恢复到另一个目录 → 运行此脚本对比
# ============================================================

SRC="${1:-$HOME/Desktop/backup_test_source}"
RESTORE="${2:-$HOME/Desktop/backup_restore_target}"

if [ ! -d "$SRC" ]; then
    echo "错误: 源目录不存在: $SRC"
    exit 1
fi
if [ ! -d "$RESTORE" ]; then
    echo "错误: 恢复目录不存在: $RESTORE"
    exit 1
fi

PASS=0
FAIL=0
SKIP=0

check() {
    # $1=描述, $2=源值, $3=恢复值, $4=期望(可选)
    local desc="$1"
    local src_val="$2"
    local rst_val="$3"
    if [ "$src_val" = "$rst_val" ]; then
        echo "  ✓ $desc: $src_val"
        PASS=$((PASS + 1))
    else
        echo "  ✗ $desc: 源=$src_val  恢复=$rst_val"
        FAIL=$((FAIL + 1))
    fi
}

echo "========================================="
echo "  元数据恢复验证"
echo "========================================="
echo "源目录:   $SRC"
echo "恢复目录: $RESTORE"
echo ""

# ══════════════════════════════════════════════════════════════
# 1. 文件权限验证
# ══════════════════════════════════════════════════════════════
echo "── 1. 文件权限 ──"

for f in readonly.txt executable.sh; do
    if [ -f "$SRC/$f" ] && [ -f "$RESTORE/$f" ]; then
        s_perm=$(stat -c '%a' "$SRC/$f")
        r_perm=$(stat -c '%a' "$RESTORE/$f")
        check "$f 权限" "$s_perm" "$r_perm"
    else
        echo "  ⚠ $f 不存在，跳过"
        SKIP=$((SKIP + 1))
    fi
done

# ══════════════════════════════════════════════════════════════
# 2. 时间戳验证 (只看修改时间)
# ══════════════════════════════════════════════════════════════
echo ""
echo "── 2. 修改时间 ──"

for f in old_archive.txt recent.txt; do
    if [ -f "$SRC/$f" ] && [ -f "$RESTORE/$f" ]; then
        s_mtime=$(stat -c '%Y' "$SRC/$f")
        r_mtime=$(stat -c '%Y' "$RESTORE/$f")
        # 允许 ±1 秒误差
        diff=$(( s_mtime - r_mtime ))
        diff=${diff#-}  # 取绝对值
        if [ "$diff" -le 1 ]; then
            echo "  ✓ $f: $(date -d @$s_mtime '+%Y-%m-%d %H:%M')"
            PASS=$((PASS + 1))
        else
            echo "  ✗ $f: 源=$(date -d @$s_mtime '+%Y-%m-%d %H:%M')  恢复=$(date -d @$r_mtime '+%Y-%m-%d %H:%M')"
            FAIL=$((FAIL + 1))
        fi
    else
        echo "  ⚠ $f 不存在，跳过"
        SKIP=$((SKIP + 1))
    fi
done

# ══════════════════════════════════════════════════════════════
# 3. 符号链接验证
# ══════════════════════════════════════════════════════════════
echo ""
echo "── 3. 符号链接 ──"

if [ -L "$SRC/link_to_readme" ]; then
    s_link=$(readlink "$SRC/link_to_readme")
    if [ -L "$RESTORE/link_to_readme" ]; then
        r_link=$(readlink "$RESTORE/link_to_readme")
        check "link_to_readme 链接目标" "$s_link" "$r_link"
    else
        echo "  ✗ link_to_readme: 源=符号链接  恢复=普通文件 (链接未保留!)"
        FAIL=$((FAIL + 1))
    fi
else
    echo "  ⚠ link_to_readme 不存在"
    SKIP=$((SKIP + 1))
fi

# ══════════════════════════════════════════════════════════════
# 4. 硬链接验证
# ══════════════════════════════════════════════════════════════
echo ""
echo "── 4. 硬链接 ──"

if [ -f "$SRC/hardlink_readme" ] && [ -f "$SRC/readme.txt" ]; then
    s_inode_link=$(stat -c '%i' "$SRC/hardlink_readme")
    s_inode_orig=$(stat -c '%i' "$SRC/readme.txt")
    if [ -f "$RESTORE/hardlink_readme" ] && [ -f "$RESTORE/readme.txt" ]; then
        r_inode_link=$(stat -c '%i' "$RESTORE/hardlink_readme")
        r_inode_orig=$(stat -c '%i' "$RESTORE/readme.txt")
        if [ "$r_inode_link" = "$r_inode_orig" ]; then
            echo "  ✓ 硬链接保留: readme.txt 和 hardlink_readme 共享 inode=$r_inode_link"
            PASS=$((PASS + 1))
        else
            echo "  ✗ 硬链接丢失! 源端共享 inode=$s_inode_link, 恢复端不同 inode: $r_inode_orig vs $r_inode_link"
            FAIL=$((FAIL + 1))
        fi
    fi
else
    echo "  ⚠ hardlink_readme 不存在"
    SKIP=$((SKIP + 1))
fi

# ══════════════════════════════════════════════════════════════
# 5. FIFO 管道验证
# ══════════════════════════════════════════════════════════════
echo ""
echo "── 5. FIFO 管道 ──"

if [ -p "$SRC/myfifo" ]; then
    if [ -p "$RESTORE/myfifo" ]; then
        echo "  ✓ myfifo: 恢复为 FIFO 管道"
        PASS=$((PASS + 1))
    else
        echo "  ✗ myfifo: 源=FIFO  恢复=非 FIFO (管道类型丢失!)"
        FAIL=$((FAIL + 1))
    fi
else
    echo "  ⚠ myfifo 不存在"
    SKIP=$((SKIP + 1))
fi

# ══════════════════════════════════════════════════════════════
# 6. 扩展属性验证
# ══════════════════════════════════════════════════════════════
echo ""
echo "── 6. 扩展属性 (xattr) ──"

if [ -f "$SRC/with_xattr.txt" ] && [ -f "$RESTORE/with_xattr.txt" ]; then
    if command -v getfattr &>/dev/null; then
        s_xattr=$(getfattr -d "$SRC/with_xattr.txt" 2>/dev/null | grep -v '^#' | grep -v '^$' | sort)
        r_xattr=$(getfattr -d "$RESTORE/with_xattr.txt" 2>/dev/null | grep -v '^#' | grep -v '^$' | sort)
        if [ -n "$s_xattr" ]; then
            check "with_xattr.txt 扩展属性" "$s_xattr" "$r_xattr"
        else
            echo "  ⚠ 源文件无 xattr (需先安装 attr 并重新生成测试文件)"
            SKIP=$((SKIP + 1))
        fi
    else
        # 用 lsattr 或用 python3 读
        if command -v python3 &>/dev/null; then
            s_xattr=$(python3 -c "
import os
try:
    for k in os.listxattr('$SRC/with_xattr.txt'):
        v = os.getxattr('$SRC/with_xattr.txt', k)
        print(f'{k}={v.decode()}')
except: pass
" 2>/dev/null)
            r_xattr=$(python3 -c "
import os
try:
    for k in os.listxattr('$RESTORE/with_xattr.txt'):
        v = os.getxattr('$RESTORE/with_xattr.txt', k)
        print(f'{k}={v.decode()}')
except: pass
" 2>/dev/null)
            if [ -n "$s_xattr" ]; then
                check "with_xattr.txt 扩展属性" "$s_xattr" "$r_xattr"
            else
                echo "  ⚠ 源文件无 xattr (需先安装 attr 并重新生成测试文件)"
                SKIP=$((SKIP + 1))
            fi
        else
            echo "  ⚠ 无法检测 xattr (getfattr/python3 不可用)"
            SKIP=$((SKIP + 1))
        fi
    fi
else
    echo "  ⚠ with_xattr.txt 不存在"
    SKIP=$((SKIP + 1))
fi

# ══════════════════════════════════════════════════════════════
# 7. ACL 验证
# ══════════════════════════════════════════════════════════════
echo ""
echo "── 7. POSIX ACL ──"

if [ -f "$SRC/with_acl.txt" ] && [ -f "$RESTORE/with_acl.txt" ]; then
    if command -v getfacl &>/dev/null; then
        s_acl=$(getfacl -c "$SRC/with_acl.txt" 2>/dev/null | grep -v '^#' | sort)
        r_acl=$(getfacl -c "$RESTORE/with_acl.txt" 2>/dev/null | grep -v '^#' | sort)
        check "with_acl.txt ACL" "$s_acl" "$r_acl"
    else
        echo "  ⚠ getfacl 不可用"
        SKIP=$((SKIP + 1))
    fi
else
    echo "  ⚠ with_acl.txt 不存在"
    SKIP=$((SKIP + 1))
fi

# ══════════════════════════════════════════════════════════════
# 8. 文件大小验证
# ══════════════════════════════════════════════════════════════
echo ""
echo "── 8. 文件内容大小 ──"

for f in empty.dat data.bin large_file.bin readme.txt config.ini; do
    if [ -f "$SRC/$f" ] && [ -f "$RESTORE/$f" ]; then
        s_size=$(stat -c '%s' "$SRC/$f")
        r_size=$(stat -c '%s' "$RESTORE/$f")
        check "$f 大小" "$s_size" "$r_size"
    else
        echo "  ⚠ $f 不存在，跳过"
        SKIP=$((SKIP + 1))
    fi
done

# ══════════════════════════════════════════════════════════════
# 9. 文件类型统计对比
# ══════════════════════════════════════════════════════════════
echo ""
echo "── 9. 文件类型统计 ──"

count_types() {
    local dir="$1"
    local reg=0 dirs=0 link=0 fifo=0 sock=0 char=0 block=0
    while IFS= read -r f; do
        if   [ -f "$f" ]; then reg=$((reg + 1))
        elif [ -d "$f" ]; then dirs=$((dirs + 1))
        elif [ -L "$f" ]; then link=$((link + 1))
        elif [ -p "$f" ]; then fifo=$((fifo + 1))
        elif [ -S "$f" ]; then sock=$((sock + 1))
        elif [ -c "$f" ]; then char=$((char + 1))
        elif [ -b "$f" ]; then block=$((block + 1))
        fi
    done < <(find "$dir" -mindepth 1 2>/dev/null)
    echo "  普通文件=$reg 目录=$dirs 符号链接=$link FIFO=$fifo 套接字=$sock 字符设备=$char 块设备=$block"
}

echo "  源端: $(count_types "$SRC")"
echo "  恢复: $(count_types "$RESTORE")"

# ══════════════════════════════════════════════════════════════
# 结果汇总
# ══════════════════════════════════════════════════════════════
echo ""
echo "========================================="
echo "  验证结果: ✓ $PASS 通过  ✗ $FAIL 失败  ⚠ $SKIP 跳过"
echo "========================================="

if [ "$FAIL" -eq 0 ]; then
    echo ""
    echo "🎉 全部通过！元数据恢复功能正常。"
else
    echo ""
    echo "🔧 有 $FAIL 项未通过，检查对应功能实现。"
fi
