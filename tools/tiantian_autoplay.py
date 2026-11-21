#!/usr/bin/env python3
"""在 macOS 微信电脑版天天象棋小程序中复用棋析的识别与分析能力。"""

import argparse
import ctypes as C
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

PROJECT_DIR = Path(__file__).resolve().parents[1]
BUILD_DIR = PROJECT_DIR / "build"
HELPER = BUILD_DIR / "autoplay_helper"
WORK_DIR = BUILD_DIR / "autoplay"
DEFAULT_CONFIG = WORK_DIR / "tiantian.json"
INITIAL_FEN = "rnbakabnr/9/1c5c1/p1p1p1p1p/9/9/P1P1P1P1P/1C5C1/9/RNBAKABNR w - - 0 1"

CORE_GRAPHICS = C.CDLL("/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics")
APPLICATION_SERVICES = C.CDLL(
    "/System/Library/Frameworks/ApplicationServices.framework/ApplicationServices"
)
CORE_FOUNDATION = C.CDLL("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation")


class CGPoint(C.Structure):
    _fields_ = [("x", C.c_double), ("y", C.c_double)]


CORE_GRAPHICS.CGEventCreate.argtypes = [C.c_void_p]
CORE_GRAPHICS.CGEventCreate.restype = C.c_void_p
CORE_GRAPHICS.CGEventGetLocation.argtypes = [C.c_void_p]
CORE_GRAPHICS.CGEventGetLocation.restype = CGPoint
CORE_GRAPHICS.CGEventCreateMouseEvent.argtypes = [
    C.c_void_p, C.c_uint32, CGPoint, C.c_uint32
]
CORE_GRAPHICS.CGEventCreateMouseEvent.restype = C.c_void_p
CORE_GRAPHICS.CGEventPost.argtypes = [C.c_uint32, C.c_void_p]
CORE_GRAPHICS.CGEventSourceButtonState.argtypes = [C.c_uint32, C.c_uint32]
CORE_GRAPHICS.CGEventSourceButtonState.restype = C.c_bool
CORE_GRAPHICS.CGPreflightScreenCaptureAccess.argtypes = []
CORE_GRAPHICS.CGPreflightScreenCaptureAccess.restype = C.c_bool
APPLICATION_SERVICES.AXIsProcessTrusted.argtypes = []
APPLICATION_SERVICES.AXIsProcessTrusted.restype = C.c_bool
CORE_FOUNDATION.CFRelease.argtypes = [C.c_void_p]


def mouse_location():
    event = CORE_GRAPHICS.CGEventCreate(None)
    if not event:
        raise RuntimeError("无法读取鼠标位置")
    try:
        point = CORE_GRAPHICS.CGEventGetLocation(event)
        return point.x, point.y
    finally:
        CORE_FOUNDATION.CFRelease(event)


def post_mouse(event_type, point, button=0):
    event = CORE_GRAPHICS.CGEventCreateMouseEvent(None, event_type, CGPoint(*point), button)
    if not event:
        raise RuntimeError("无法创建鼠标事件")
    try:
        CORE_GRAPHICS.CGEventPost(0, event)
    finally:
        CORE_FOUNDATION.CFRelease(event)


def click(point):
    post_mouse(5, point)
    time.sleep(0.08)
    post_mouse(1, point)
    time.sleep(0.06)
    post_mouse(2, point)


def wait_for_click(timeout=60):
    deadline = time.monotonic() + timeout
    while CORE_GRAPHICS.CGEventSourceButtonState(1, 0):
        if time.monotonic() >= deadline:
            raise TimeoutError("等待鼠标松开超时")
        time.sleep(0.02)
    while time.monotonic() < deadline:
        if CORE_GRAPHICS.CGEventSourceButtonState(1, 0):
            point = mouse_location()
            while CORE_GRAPHICS.CGEventSourceButtonState(1, 0):
                time.sleep(0.02)
            return point
        time.sleep(0.02)
    raise TimeoutError("等待校准点击超时")


def activate_mini_program():
    result = subprocess.run(
        [
            "/usr/bin/osascript",
            "-e",
            'tell application "System Events" to set frontmost of process "WeChatAppEx" to true',
        ],
        capture_output=True,
        text=True,
        timeout=10,
    )
    if result.returncode != 0:
        raise RuntimeError(
            "无法切换到天天象棋窗口。请确认小程序已打开，并允许终端使用“辅助功能”。\n"
            + result.stderr.strip()
        )
    time.sleep(0.30)


def check_permissions(for_clicks):
    if not CORE_GRAPHICS.CGPreflightScreenCaptureAccess():
        raise RuntimeError(
            "缺少“屏幕录制与系统录音”权限。请在 macOS 隐私与安全性设置中允许当前终端，然后重启终端。"
        )
    if for_clicks and not APPLICATION_SERVICES.AXIsProcessTrusted():
        raise RuntimeError(
            "缺少“辅助功能”权限。请在 macOS 隐私与安全性设置中允许当前终端，然后重启终端。"
        )


def load_config(path):
    try:
        config = json.loads(path.read_text(encoding="utf-8"))
        top_left = tuple(float(value) for value in config["top_left"])
        bottom_right = tuple(float(value) for value in config["bottom_right"])
        if len(top_left) != 2 or len(bottom_right) != 2:
            raise ValueError
        if bottom_right[0] <= top_left[0] or bottom_right[1] <= top_left[1]:
            raise ValueError
        return {
            "top_left": top_left,
            "bottom_right": bottom_right,
            "red_bottom": bool(config.get("red_bottom", True)),
        }
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
        raise RuntimeError(f"坐标配置无效，请先运行 calibrate：{path}") from error


def run_helper(*arguments, timeout=50):
    if not HELPER.is_file():
        raise RuntimeError(
            f"找不到本地辅助程序，请先构建：cmake --build {BUILD_DIR} --target autoplay_helper"
        )
    result = subprocess.run(
        [str(HELPER), *(str(argument) for argument in arguments)],
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    try:
        payload = json.loads(result.stdout.strip())
    except json.JSONDecodeError as error:
        raise RuntimeError(
            f"本地辅助程序输出异常（退出码 {result.returncode}）：{result.stderr.strip()}"
        ) from error
    if result.returncode != 0 or not payload.get("ok"):
        raise RuntimeError(payload.get("error", result.stderr.strip() or "本地辅助程序失败"))
    return payload


def with_turn(fen, red_turn):
    fields = fen.split()
    fields[1] = "w" if red_turn else "b"
    return " ".join(fields)


def placement(fen):
    return fen.split()[0]


def red_turn(fen):
    return fen.split()[1] == "w"


def capture_and_recognize(directory):
    activate_mini_program()
    screenshot = Path(directory) / "current.png"
    result = subprocess.run(
        ["/usr/sbin/screencapture", "-x", str(screenshot)],
        capture_output=True,
        text=True,
        timeout=15,
    )
    if result.returncode != 0 or not screenshot.is_file():
        raise RuntimeError("系统截图失败：" + result.stderr.strip())
    recognized = run_helper("recognize", screenshot, timeout=45)
    recognized["screenshot"] = screenshot
    return recognized


def capture_clear_position(directory, args, attempts=3):
    recognized = None
    for attempt in range(attempts):
        recognized = capture_and_recognize(directory)
        if recognized["minimum_piece_confidence"] >= args.min_confidence:
            return recognized
        if attempt + 1 < attempts:
            time.sleep(0.5)
    debug = save_debug(recognized)
    suffix = f"\n已保留最后截图：{debug}" if debug else ""
    raise RuntimeError("棋子识别置信度不足，未进行分析或点击" + suffix)


def move_point(uci_square, config):
    col = ord(uci_square[0]) - ord("a")
    row = 9 - int(uci_square[1])
    if not config["red_bottom"]:
        row = 9 - row
        col = 8 - col
    left, top = config["top_left"]
    right, bottom = config["bottom_right"]
    return left + (right - left) * col / 8, top + (bottom - top) * row / 9


def save_debug(recognized):
    WORK_DIR.mkdir(parents=True, exist_ok=True)
    screenshot = recognized.get("screenshot") if recognized else None
    if screenshot and Path(screenshot).is_file():
        target = WORK_DIR / "last-error.png"
        shutil.copyfile(screenshot, target)
        return target
    return None


def calibrate(args):
    check_permissions(True)
    red_bottom = input("红方是否在棋盘下方？[Y/n] ").strip().lower() not in {"n", "no"}
    print("按回车后，将在天天象棋中依次点击两个校准点：")
    print("  1. 棋盘最上排最左侧交叉点（棋盘左上角）")
    print("  2. 棋盘最下排最右侧交叉点（棋盘右下角）")
    print("这两次点击只用于记录坐标，不会完成一步走棋。")
    input("准备好后按回车，再切换到弹出的天天象棋窗口完成两次点击…")
    activate_mini_program()
    top_left = wait_for_click()
    bottom_right = wait_for_click()
    if bottom_right[0] - top_left[0] < 200 or bottom_right[1] - top_left[1] < 300:
        raise RuntimeError(f"校准点距离异常：{top_left} -> {bottom_right}")
    args.config.parent.mkdir(parents=True, exist_ok=True)
    args.config.write_text(
        json.dumps(
            {"top_left": top_left, "bottom_right": bottom_right, "red_bottom": red_bottom},
            ensure_ascii=False,
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    print(f"坐标已保存到 {args.config}")


def check(args):
    check_permissions(False)
    config = load_config(args.config)
    with tempfile.TemporaryDirectory(prefix="tiantian-autoplay-") as directory:
        recognized = capture_clear_position(directory, args)
        print(recognized["details"].splitlines()[0])
        print("识别 FEN：", recognized["fen"])
        fen = with_turn(recognized["fen"], args.side == "red")
        analysis = run_helper("analyze", fen, args.move_time, timeout=args.move_time / 1000 + 35)
        if not analysis.get("bestmove"):
            print("当前局面没有合法走法。")
            return
        move = analysis["bestmove"]
        print(f"建议走法：{analysis['notation']}（{move}）")
        print("起点坐标：", tuple(round(v, 1) for v in move_point(move[:2], config)))
        print("终点坐标：", tuple(round(v, 1) for v in move_point(move[2:], config)))
        print("演练模式未点击鼠标。")


def wait_for_change(expected_fen, args, directory):
    deadline = time.monotonic() + args.wait_timeout
    last_error = "棋盘尚未变化"
    last_recognized = None
    while time.monotonic() < deadline:
        try:
            recognized = capture_and_recognize(directory)
            last_recognized = recognized
            observed_fen = recognized["fen"]
            if recognized["minimum_piece_confidence"] < args.min_confidence:
                last_error = "棋子识别置信度不足"
            elif placement(observed_fen) == placement(expected_fen):
                return expected_fen, None, recognized
            else:
                try:
                    transition = run_helper("transition", expected_fen, observed_fen)
                    return transition["after_fen"], transition["move"], recognized
                except RuntimeError as error:
                    last_error = str(error)
        except (RuntimeError, subprocess.TimeoutExpired) as error:
            last_error = str(error)
        time.sleep(args.poll_interval)
    debug = save_debug(last_recognized)
    suffix = f"\n已保留最后截图：{debug}" if debug else ""
    raise RuntimeError(f"等待棋局变化超时：{last_error}{suffix}")


def play(args):
    check_permissions(True)
    config = load_config(args.config)
    our_red = args.side == "red"
    with tempfile.TemporaryDirectory(prefix="tiantian-autoplay-") as directory:
        recognized = capture_clear_position(directory, args)
        if args.from_current:
            expected_fen = with_turn(recognized["fen"], our_red)
            print("将从当前局面继续，并假定现在轮到己方。按 Ctrl+C 可随时停止。", flush=True)
        else:
            if placement(recognized["fen"]) != placement(INITIAL_FEN):
                debug = save_debug(recognized)
                raise RuntimeError(
                    "自动对弈须从双方未走子的初始局面开始。请在小程序中新开一局后再运行，"
                    "或在轮到己方时使用 --from-current。"
                    + (f"\n当前截图：{debug}" if debug else "")
                )
            expected_fen = INITIAL_FEN
            print("已确认初始局面。按 Ctrl+C 可随时停止。", flush=True)
        move_number = 0
        while move_number < args.max_moves:
            if red_turn(expected_fen) != our_red:
                expected_fen, opponent_move, _ = wait_for_change(expected_fen, args, directory)
                if opponent_move:
                    print(f"对手走棋：{opponent_move}", flush=True)
                if red_turn(expected_fen) != our_red:
                    continue

            analysis = run_helper(
                "analyze", expected_fen, args.move_time, timeout=args.move_time / 1000 + 35
            )
            if not analysis.get("bestmove"):
                print("当前没有合法走法，对局结束。")
                return
            move_number += 1
            move = analysis["bestmove"]
            source = move_point(move[:2], config)
            target = move_point(move[2:], config)
            print(f"第 {move_number} 手：{analysis['notation']}（{move}）", flush=True)
            activate_mini_program()
            click(source)
            time.sleep(args.click_delay)
            click(target)
            expected_fen = analysis["after_fen"]

            # 确认自己这一步已经落盘，避免点击失效后重复操作。
            expected_fen, _, _ = wait_for_change(expected_fen, args, directory)
        print(f"已完成设定的 {args.max_moves} 手，脚本停止。")


def main():
    parser = argparse.ArgumentParser(
        description="复用棋析的本地识别与分析，在微信电脑版天天象棋小程序中辅助对弈。"
    )
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG, help="棋盘坐标配置文件")
    subparsers = parser.add_subparsers(dest="command", required=True)

    calibration_parser = subparsers.add_parser("calibrate", help="记录棋盘两个角的鼠标坐标")
    calibration_parser.set_defaults(function=calibrate)

    for name, help_text, function in [
        ("check", "只识别并给出建议，不点击鼠标", check),
        ("play", "从初始局面自动对弈", play),
    ]:
        command = subparsers.add_parser(name, help=help_text)
        command.add_argument("--side", choices=["red", "black"], default="red", help="己方执红或执黑")
        command.add_argument("--move-time", type=int, default=1000, help="每手分析毫秒数")
        command.set_defaults(function=function)
        if name == "play":
            command.add_argument("--from-current", action="store_true", help="从当前局面继续，假定轮到己方")
            command.add_argument("--max-moves", type=int, default=200, help="最多自动走多少手")
            command.add_argument("--wait-timeout", type=float, default=45, help="等待落子的秒数")
            command.add_argument("--poll-interval", type=float, default=0.7, help="截图轮询间隔秒数")
            command.add_argument("--click-delay", type=float, default=0.18, help="两次点击间隔秒数")
        command.add_argument("--min-confidence", type=float, default=0.65, help="最低棋子置信度")

    args = parser.parse_args()
    if args.command != "calibrate" and not 200 <= args.move_time <= 60000:
        parser.error("--move-time 必须在 200 到 60000 之间")
    try:
        args.function(args)
    except KeyboardInterrupt:
        print("\n已由用户停止。", file=sys.stderr)
        return 130
    except (RuntimeError, TimeoutError, subprocess.TimeoutExpired) as error:
        print(f"错误：{error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
