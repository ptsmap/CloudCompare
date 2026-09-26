#!/bin/bash
#
# VSG 渲染后端冒烟测试
#
# 启动 CloudCompare，自动创建 VSG 视图并打开指定的点云/网格文件，
# 渲染一帧后把结果保存为 PNG，然后退出。
#
# 用法:
#   ./scripts/vsg_smoke_test.sh <文件.ply> [输出.png]
#
# 依赖环境变量（由本脚本设置）:
#   CC_VSG_VIEW=1              启动后自动创建 VSG 3D 视图
#   CC_VSG_SCREENSHOT=<path>   渲染并保存截图后退出
#
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

APP="$PROJECT_DIR/build-hbqt/qCC/CloudCompare.app/Contents/MacOS/CloudCompare"
INPUT="${1:?用法: $0 <文件.ply> [输出.png]}"
OUTPUT="${2:-/tmp/vsg_smoke.png}"
LOG="${OUTPUT%.png}.log"

if [ ! -x "$APP" ]; then
	echo "错误: 找不到可执行文件 $APP" >&2
	echo "请先构建: cmake --build build-hbqt --target CloudCompare" >&2
	exit 1
fi

if [ ! -f "$INPUT" ]; then
	echo "错误: 输入文件不存在: $INPUT" >&2
	exit 1
fi

rm -f "$OUTPUT" "$LOG"

# 确保没有残留的 CloudCompare 进程干扰
if pgrep -f "CloudCompare.app/Contents/MacOS/CloudCompare" > /dev/null 2>&1; then
	echo "警告: 已有 CloudCompare 在运行，先退出它"
	osascript -e 'quit app "CloudCompare"' > /dev/null 2>&1 || true
	sleep 2
fi

echo "== 启动 CloudCompare (VSG) =="
echo "   输入: $INPUT"
echo "   输出: $OUTPUT"

CC_VSG_VIEW=1 CC_VSG_SCREENSHOT="$OUTPUT" "$APP" "$INPUT" > "$LOG" 2>&1 &
PID=$!

# 等待截图生成或进程退出（最多 90 秒）
for _ in $(seq 1 90); do
	if [ -f "$OUTPUT" ]; then
		break
	fi
	if ! kill -0 "$PID" 2>/dev/null; then
		break
	fi
	sleep 1
done

# 收尾
kill "$PID" > /dev/null 2>&1 || true
wait "$PID" > /dev/null 2>&1 || true

if [ -f "$OUTPUT" ]; then
	echo "== 成功 =="
	sips -g pixelWidth -g pixelHeight "$OUTPUT" 2>/dev/null | tail -2
	exit 0
fi

echo "== 失败: 未生成截图 ==" >&2
echo "---- 日志 (末尾 40 行) ----" >&2
tail -40 "$LOG" >&2
exit 1
