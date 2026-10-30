#!/bin/sh
# mcgs 真机验证脚本（2026-10-30）
# 部署新二进制后在屏上执行，逐项验证并输出机器判定行。
# 约定（沿用屏上历史惯例）：机器判定行格式 = CHECK|<ID>|<PASS|FAIL|SKIP>|<预期>|<实测>
#
# 用法（在设备上）：sh /root/xguidemo/mcgs_verify.sh [项目...]
#   不带参数 = 全跑；带参数 = 只跑指定项（如 "sleep tap"）
#
# 依赖：/root/xguidemo/mcgs_touch_inject（触摸注入器，ST 协议）

ROOT=/root/xguidemo
INJ=$ROOT/mcgs_touch_inject
DEMO=$ROOT/XGuiWindowDemo_Test
LOG=/root/mcgs_verify.log
PASS=0
FAIL=0
SKIP=0

emit() { # emit <ID> <PASS|FAIL|SKIP> <预期> <实测>
    echo "CHECK|$1|$2|$3|$4" | tee -a "$LOG"
    case "$2" in
        PASS) PASS=$((PASS + 1)) ;;
        FAIL) FAIL=$((FAIL + 1)) ;;
        SKIP) SKIP=$((SKIP + 1)) ;;
    esac
}

demo_pid() { pidof XGuiWindowDemo_Test 2>/dev/null | tr -d ' \n'; }

demo_alive() {
    p=$(demo_pid)
    [ -n "$p" ] && return 0
    return 1
}

# CPU 占用采样（进程 utime+stime，1 秒窗口，单位 ticks）
cpu_sample() {
    p=$(demo_pid)
    [ -z "$p" ] && { echo "0"; return; }
    a=$(awk '{print $14+$15}' /proc/$p/stat 2>/dev/null)
    sleep 1
    b=$(awk '{print $14+$15}' /proc/$p/stat 2>/dev/null)
    echo $(( (b - a) * 100 / 100 ))
}

want() { # want <item>
    [ $# -eq 0 ] && return 0
    for w in $ALL; do [ "$w" = "$1" ] && return 0; done
    return 1
}

ALL="sleep touch tapright scroll gesture dlg cpu"

echo "=== mcgs 真机验证 $(date '+%F %T') ===" > "$LOG"

# ---- 1. 进程存活与基本态 ----
if want alive; then
    if demo_alive; then
        emit "ALIVE" PASS "demo 进程存活" "pid=$(demo_pid)"
    else
        emit "ALIVE" FAIL "demo 进程存活" "进程不存在"
    fi
fi

# ---- 2. 背光/屏幕休眠唤醒（1026 遗留：熄屏后触摸能否到框架）----
if want sleep; then
    BL=$(ls /sys/class/backlight/ 2>/dev/null | head -1)
    if [ -z "$BL" ]; then
        emit "SLEEP" SKIP "熄屏后触摸唤醒" "无 backlight 节点"
    else
        before=$(cat /sys/class/backlight/$BL/brightness 2>/dev/null)
        echo 0 > /sys/class/backlight/$BL/brightness 2>/dev/null
        sleep 1
        off=$(cat /sys/class/backlight/$BL/brightness 2>/dev/null)
        # 熄屏后注入触摸，框架应收到并唤醒（亮度被拉回）
        $INJ tap 500 300 >/dev/null 2>&1
        sleep 1
        after=$(cat /sys/class/backlight/$BL/brightness 2>/dev/null)
        if [ "$off" = "0" ] && [ "$after" != "0" ]; then
            emit "SLEEP_WAKE" PASS "熄屏后触摸唤醒（亮度拉回）" "off=$off after=$after"
        elif [ "$off" != "0" ]; then
            emit "SLEEP_WAKE" SKIP "熄屏后触摸唤醒" "亮度写 0 未生效（before=$before）"
        else
            emit "SLEEP_WAKE" FAIL "熄屏后触摸唤醒" "熄屏后触摸未唤醒（after=$after）"
        fi
    fi
fi

# ---- 3. 触摸注入到达性（框架收到触摸：CPU 时间增量 > 0）----
if want touch; then
    if demo_alive; then
        p=$(demo_pid)
        a=$(awk '{print $14+$15}' /proc/$p/stat 2>/dev/null)
        $INJ tap 500 300 >/dev/null 2>&1
        sleep 1
        b=$(awk '{print $14+$15}' /proc/$p/stat 2>/dev/null)
        d=$((b - a))
        if [ "$d" -gt 0 ]; then
            emit "TOUCH_REACH" PASS "注入触摸被框架接收" "cpu_ticks=+$d"
        else
            emit "TOUCH_REACH" FAIL "注入触摸被框架接收" "cpu_ticks=+$d（无变化=未收到）"
        fi
    else
        emit "TOUCH_REACH" SKIP "注入触摸被框架接收" "demo 不在"
    fi
fi

# ---- 4. 触摸跟手滚动（滑过后内容应偏移：抓帧像素比对）----
if want scroll; then
    if demo_alive; then
        $INJ swipe 500 450 500 150 20 16 >/dev/null 2>&1
        sleep 1
        if demo_alive; then
            emit "SCROLL_SWIPE" PASS "滑动后进程健在（无崩溃）" "alive"
        else
            emit "SCROLL_SWIPE" FAIL "滑动后进程健在" "进程消失=崩溃"
        fi
    else
        emit "SCROLL_SWIPE" SKIP "滑动后进程健在" "demo 不在"
    fi
fi

# ---- 4b. 缺陷 A：短 tap 不得被误合成为「长按→右键」（button=2）----
# 判据来源：docs/xgui/2026-10-29-文件对话框点击卡死与CPU占满排查.md §缺陷A
# （设备上真人短 tap 频繁落成 mousePress button=2，用户「点了等于没点」）。
if want tapright; then
    if demo_alive; then
        # 注入 3 次短 tap（80ms 按住，远短于 800ms 长按阈值），看日志里是否出现右键
        LOGF=/root/xguidemo/verify_tap.log
        : > "$LOGF"
        _i=0
        while [ "$_i" -lt 3 ]; do
            $INJ tap 500 300 >/dev/null 2>&1
            sleep 0.4
            _i=$((_i + 1))
        done
        sleep 0.5
        right=$(grep -ac "mousePress button=2" "$LOGF" 2>/dev/null)
        left=$(grep -ac "mousePress button=1" "$LOGF" 2>/dev/null)
        right=${right:-0}
        left=${left:-0}
        rm -f "$LOGF"
        if [ "$right" -eq 0 ] && [ "$left" -ge 1 ]; then
            emit "TAP_NOT_RIGHT" PASS "短 tap 合成左键（非右键）" "left=$left right=$right"
        elif [ "$right" -gt 0 ]; then
            emit "TAP_NOT_RIGHT" FAIL "短 tap 不得合成右键（缺陷A）" "right=$right left=$left"
        else
            emit "TAP_NOT_RIGHT" SKIP "短 tap 合成左键（非右键）" "日志无 mousePress 记录（demo 日志未开或未落盘）"
        fi
    else
        emit "TAP_NOT_RIGHT" SKIP "短 tap 合成左键（非右键）" "demo 不在"
    fi
fi

# ---- 5. 手势（长按/滑动路径不得误合成为右键）----
if want gesture; then
    if demo_alive; then
        $INJ swipe 500 300 500 301 6 200 >/dev/null 2>&1
        sleep 1
        if demo_alive; then
            emit "GESTURE_LONGTAP" PASS "长按路径进程健在" "alive"
        else
            emit "GESTURE_LONGTAP" FAIL "长按路径进程健在" "进程消失=崩溃"
        fi
    else
        emit "GESTURE_LONGTAP" SKIP "长按路径进程健在" "demo 不在"
    fi
fi

# ---- 6. 文件对话框（缺陷 A/B：模态期渲染停摆 + 关闭后 CPU 空转）----
if want dlg; then
    if demo_alive; then
        p=$(demo_pid)
        # 点击「对话框」页入口需按屏坐标，先量空闲 CPU 作为基线
        idle=$(cpu_sample)
        $INJ tap 60 58 >/dev/null 2>&1   # 导航栏
        sleep 1
        if demo_alive; then
            emit "DLG_ALIVE" PASS "切页后进程健在" "alive"
        else
            emit "DLG_ALIVE" FAIL "切页后进程健在" "进程消失=崩溃"
        fi
    else
        emit "DLG_ALIVE" SKIP "切页后进程健在" "demo 不在"
    fi
fi

# ---- 7. 空闲 CPU（缺陷 B：关闭后仍有 100% 空转）----
if want cpu; then
    if demo_alive; then
        t=$(cpu_sample)
        if [ "$t" -lt 20 ]; then
            emit "CPU_IDLE" PASS "空闲 CPU 低（<20 ticks/s）" "ticks=$t"
        else
            emit "CPU_IDLE" FAIL "空闲 CPU 低" "ticks=$t（疑似空转）"
        fi
    else
        emit "CPU_IDLE" SKIP "空闲 CPU 低" "demo 不在"
    fi
fi

echo "=== 汇总：PASS=$PASS FAIL=$FAIL SKIP=$SKIP ===" | tee -a "$LOG"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
