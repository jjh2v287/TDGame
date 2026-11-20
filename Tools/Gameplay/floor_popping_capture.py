"""실행 환경: 시스템 Python(Windows, numpy·Pillow·scipy). pie_floor_popping.py 가 가져다 쓰는 화면 영역 복사·뷰포트 위치 찾기 모듈이며, 단독 실행은 자체 시험만 한다.
실행 명령: python -B Tools/Gameplay/floor_popping_capture.py --self-test
출력: 자체 시험은 표준 출력(영역 복사 시간·위치 찾기 오차)과 종료 코드 0/1. 모듈로 쓰면 run 폴더의 grabs/grab_<00000>.jpg(품질 95, 4:4:4 — PNG 는 마을 경로 1회 4.3GB 라 디스크를 채웠다)·grabs.csv·capture.json
상태: GF-07(2026-11-20). 엔진 Shot 은 장당 약 200ms 로 게임 스레드를 멈춰 측정을 왜곡하므로 걷는 동안의 촬영은 이 모듈이 맡는다.
"""
import argparse
import csv
import ctypes
import ctypes.wintypes as wintypes
import json
import os
import queue
import sys
import threading
import time

import numpy as np
from PIL import Image
from scipy.signal import fftconvolve

SRCCOPY = 0x00CC0020
DIB_RGB_COLORS = 0
BI_RGB = 0
LOCATE_SCALE = 4
REFINE_RADIUS_PX = 6
PNG_COMPRESS_LEVEL = 1
JPEG_QUALITY = 95
WRITER_THREADS = 3
QUEUE_LIMIT = 48
EDITOR_PROCESS_NAME = "UnrealEditor.exe"


class BitmapInfoHeader(ctypes.Structure):
    _fields_ = [
        ("biSize", wintypes.DWORD), ("biWidth", wintypes.LONG), ("biHeight", wintypes.LONG), ("biPlanes", wintypes.WORD),
        ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD), ("biSizeImage", wintypes.DWORD),
        ("biXPelsPerMeter", wintypes.LONG), ("biYPelsPerMeter", wintypes.LONG), ("biClrUsed", wintypes.DWORD), ("biClrImportant", wintypes.DWORD),
    ]


class BitmapInfo(ctypes.Structure):
    _fields_ = [("bmiHeader", BitmapInfoHeader), ("bmiColors", wintypes.DWORD * 3)]


def make_process_dpi_aware():
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(2)
    except OSError:
        ctypes.windll.user32.SetProcessDPIAware()


def declare_gdi_signatures():
    user32, gdi32 = ctypes.windll.user32, ctypes.windll.gdi32
    user32.GetDC.argtypes = [wintypes.HWND]
    user32.GetDC.restype = wintypes.HDC
    user32.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]
    gdi32.CreateCompatibleDC.argtypes = [wintypes.HDC]
    gdi32.CreateCompatibleDC.restype = wintypes.HDC
    gdi32.CreateDIBSection.argtypes = [wintypes.HDC, ctypes.c_void_p, wintypes.UINT, ctypes.POINTER(ctypes.c_void_p), wintypes.HANDLE, wintypes.DWORD]
    gdi32.CreateDIBSection.restype = wintypes.HBITMAP
    gdi32.SelectObject.argtypes = [wintypes.HDC, wintypes.HGDIOBJ]
    gdi32.SelectObject.restype = wintypes.HGDIOBJ
    gdi32.BitBlt.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.HDC, ctypes.c_int, ctypes.c_int, wintypes.DWORD]
    gdi32.BitBlt.restype = wintypes.BOOL
    gdi32.DeleteObject.argtypes = [wintypes.HGDIOBJ]
    gdi32.DeleteDC.argtypes = [wintypes.HDC]
    return user32, gdi32


class RegionGrabber:
    def __init__(self, left, top, width, height):
        self.left, self.top, self.width, self.height = int(left), int(top), int(width), int(height)
        user32, gdi32 = declare_gdi_signatures()
        self.screen_dc = user32.GetDC(None)
        self.memory_dc = gdi32.CreateCompatibleDC(self.screen_dc)
        info = BitmapInfo()
        info.bmiHeader.biSize = ctypes.sizeof(BitmapInfoHeader)
        info.bmiHeader.biWidth = self.width
        info.bmiHeader.biHeight = -self.height
        info.bmiHeader.biPlanes = 1
        info.bmiHeader.biBitCount = 32
        info.bmiHeader.biCompression = BI_RGB
        bits = ctypes.c_void_p()
        self.bitmap = gdi32.CreateDIBSection(self.memory_dc, ctypes.byref(info), DIB_RGB_COLORS, ctypes.byref(bits), None, 0)
        if not self.bitmap or not bits.value:
            raise OSError("CreateDIBSection failed")
        self.previous = gdi32.SelectObject(self.memory_dc, self.bitmap)
        self.pixels = (ctypes.c_ubyte * (self.width * self.height * 4)).from_address(bits.value)

    def grab(self):
        if not ctypes.windll.gdi32.BitBlt(self.memory_dc, 0, 0, self.width, self.height, self.screen_dc, self.left, self.top, SRCCOPY):
            raise OSError("BitBlt failed")
        bgra = np.frombuffer(self.pixels, dtype=np.uint8).reshape(self.height, self.width, 4)
        return bgra[:, :, 2::-1].copy()

    def close(self):
        gdi32 = ctypes.windll.gdi32
        gdi32.SelectObject(self.memory_dc, self.previous)
        gdi32.DeleteObject(self.bitmap)
        gdi32.DeleteDC(self.memory_dc)
        ctypes.windll.user32.ReleaseDC(None, self.screen_dc)


def find_process_ids(image_name):
    snapshot = os.popen(f'tasklist /FI "IMAGENAME eq {image_name}" /FO CSV /NH').read()
    ids = []
    for line in snapshot.splitlines():
        parts = [part.strip('"') for part in line.split('","')]
        if len(parts) > 1 and parts[1].isdigit():
            ids.append(int(parts[1]))
    return ids


def find_largest_window(process_ids):
    user32 = ctypes.windll.user32
    found = []

    @ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)
    def visit(handle, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(handle, ctypes.byref(owner))
        if owner.value in process_ids and user32.IsWindowVisible(handle) and not user32.IsIconic(handle):
            rect = wintypes.RECT()
            user32.GetWindowRect(handle, ctypes.byref(rect))
            found.append((handle, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top))
        return True

    user32.EnumWindows(visit, 0)
    if not found:
        return None
    return max(found, key=lambda window: window[3] * window[4])


def to_gray(rgb):
    return (rgb[..., 0] * 0.299 + rgb[..., 1] * 0.587 + rgb[..., 2] * 0.114).astype(np.float32)


def downsample(gray, factor):
    height, width = gray.shape[0] // factor * factor, gray.shape[1] // factor * factor
    return gray[:height, :width].reshape(height // factor, factor, width // factor, factor).mean(axis=(1, 3))


def normalized_cross_correlation(image, template):
    template = template - template.mean()
    template_norm = np.sqrt((template ** 2).sum())
    ones = np.ones(template.shape, dtype=np.float32)
    numerator = fftconvolve(image, template[::-1, ::-1], mode="valid")
    local_sum = fftconvolve(image, ones, mode="valid")
    local_square = fftconvolve(image ** 2, ones, mode="valid")
    local_variance = np.maximum(local_square - local_sum ** 2 / template.size, 1e-6)
    return numerator / (np.sqrt(local_variance) * max(template_norm, 1e-6))


def locate_template(window_rgb, template_rgb):
    window_gray, template_gray = to_gray(window_rgb), to_gray(template_rgb)
    if template_gray.shape[0] > window_gray.shape[0] or template_gray.shape[1] > window_gray.shape[1]:
        return None
    coarse = normalized_cross_correlation(downsample(window_gray, LOCATE_SCALE), downsample(template_gray, LOCATE_SCALE))
    peak_y, peak_x = np.unravel_index(int(np.argmax(coarse)), coarse.shape)
    best = None
    crop = template_gray[template_gray.shape[0] // 4: template_gray.shape[0] * 3 // 4, template_gray.shape[1] // 4: template_gray.shape[1] * 3 // 4]
    crop_top, crop_left = template_gray.shape[0] // 4, template_gray.shape[1] // 4
    for dy in range(-REFINE_RADIUS_PX, REFINE_RADIUS_PX + 1):
        for dx in range(-REFINE_RADIUS_PX, REFINE_RADIUS_PX + 1):
            top, left = peak_y * LOCATE_SCALE + dy, peak_x * LOCATE_SCALE + dx
            if top < 0 or left < 0 or top + template_gray.shape[0] > window_gray.shape[0] or left + template_gray.shape[1] > window_gray.shape[1]:
                continue
            patch = window_gray[top + crop_top: top + crop_top + crop.shape[0], left + crop_left: left + crop_left + crop.shape[1]]
            error = float(np.mean(np.abs(patch - crop)))
            if best is None or error < best[0]:
                best = (error, left, top)
    if best is None:
        return None
    patch = window_gray[best[2]: best[2] + template_gray.shape[0], best[1]: best[1] + template_gray.shape[1]]
    correlation = float(np.corrcoef(patch.ravel(), template_gray.ravel())[0, 1])
    return {"left": best[1], "top": best[2], "mean_abs_error": best[0], "correlation": correlation, "coarse_score": float(coarse.max())}


def locate_viewport(template_path):
    template = np.asarray(Image.open(template_path).convert("RGB"))
    window = find_largest_window(find_process_ids(EDITOR_PROCESS_NAME))
    if window is None:
        return {"ok": False, "reason": "editor window not found"}
    handle, window_left, window_top, window_width, window_height = window
    grabber = RegionGrabber(window_left, window_top, window_width, window_height)
    try:
        window_image = grabber.grab()
    finally:
        grabber.close()
    match = locate_template(window_image, template)
    if match is None:
        return {"ok": False, "reason": "template larger than window", "window": [window_left, window_top, window_width, window_height]}
    foreground = ctypes.windll.user32.GetForegroundWindow() == handle
    return {
        "ok": match["correlation"] >= 0.8, "left": int(window_left + match["left"]), "top": int(window_top + match["top"]),
        "width": int(template.shape[1]), "height": int(template.shape[0]), "window": [window_left, window_top, window_width, window_height],
        "editor_in_foreground": bool(foreground), "offset_in_window": [int(match["left"]), int(match["top"])],
        "correlation": match["correlation"], "mean_abs_error": match["mean_abs_error"], "coarse_score": match["coarse_score"],
    }


class CaptureSession:
    def __init__(self, run_dir, rect, target_hz, image_format="jpg"):
        self.run_dir = run_dir
        self.image_format = image_format
        self.rect = rect
        self.period = 1.0 / max(target_hz, 0.1)
        self.directory = os.path.join(run_dir, "grabs")
        os.makedirs(self.directory, exist_ok=True)
        self.stop_event = threading.Event()
        self.queue = queue.Queue(maxsize=QUEUE_LIMIT)
        self.rows = []
        self.errors = []
        self.dropped = 0
        self.thread = threading.Thread(target=self.run_grab_loop, daemon=True)
        self.writers = [threading.Thread(target=self.run_writer, daemon=True) for _ in range(WRITER_THREADS)]

    def start(self):
        for writer in self.writers:
            writer.start()
        self.thread.start()

    def run_grab_loop(self):
        grabber = RegionGrabber(self.rect["left"], self.rect["top"], self.rect["width"], self.rect["height"])
        try:
            number = 0
            next_time = time.perf_counter()
            while not self.stop_event.is_set():
                now = time.perf_counter()
                if now < next_time:
                    time.sleep(min(next_time - now, 0.005))
                    continue
                next_time += self.period
                if next_time < now:
                    next_time = now + self.period
                start_epoch = time.time()
                image = grabber.grab()
                end_epoch = time.time()
                path = os.path.join(self.directory, f"grab_{number:05d}.{self.image_format}")
                try:
                    self.queue.put_nowait((path, image))
                except queue.Full:
                    self.dropped += 1
                    continue
                self.rows.append((number, start_epoch, end_epoch, path.replace("\\", "/")))
                number += 1
        except Exception as error:
            self.errors.append(f"grab: {error!r}")
        finally:
            grabber.close()
            for _ in self.writers:
                self.queue.put(None)

    def run_writer(self):
        while True:
            item = self.queue.get()
            if item is None:
                return
            path, image = item
            try:
                save_grab(image, path)
            except Exception as error:
                self.errors.append(f"write {os.path.basename(path)}: {error!r}")

    def stop(self):
        self.stop_event.set()
        self.thread.join(timeout=10)
        for writer in self.writers:
            writer.join(timeout=60)
        with open(os.path.join(self.run_dir, "grabs.csv"), "w", newline="", encoding="utf-8") as handle:
            writer = csv.writer(handle)
            writer.writerow(["n", "epoch_start", "epoch_end", "path"])
            writer.writerows(self.rows)
        return {"grabs": len(self.rows), "dropped": self.dropped, "errors": self.errors}


def save_grab(image, path):
    if path.endswith(".jpg"):
        Image.fromarray(image).save(path, quality=JPEG_QUALITY, subsampling=0)
        return
    Image.fromarray(image).save(path, compress_level=PNG_COMPRESS_LEVEL)


def write_capture_report(run_dir, payload):
    with open(os.path.join(run_dir, "capture.json"), "w", encoding="utf-8") as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=1)


def run_self_test():
    make_process_dpi_aware()
    grabber = RegionGrabber(0, 0, 640, 360)
    timings = []
    try:
        for _ in range(20):
            start = time.perf_counter()
            image = grabber.grab()
            timings.append((time.perf_counter() - start) * 1000.0)
    finally:
        grabber.close()
    rng = np.random.default_rng(7)
    window = (rng.random((420, 900, 3)) * 255).astype(np.uint8)
    window[...] = np.clip(window.astype(np.int32) // 3 + 80, 0, 255).astype(np.uint8)
    template = window[57:57 + 300, 133:133 + 600].copy()
    match = locate_template(window, template)
    passed = match is not None and match["left"] == 133 and match["top"] == 57 and match["correlation"] > 0.99
    print(f"grab 640x360 ms median={sorted(timings)[len(timings) // 2]:.1f} max={max(timings):.1f} shape={image.shape}")
    print(f"locate expected=(133,57) got={None if match is None else (match['left'], match['top'])} corr={None if match is None else round(match['correlation'], 4)}")
    print("SELF-TEST PASSED" if passed else "SELF-TEST FAILED")
    return 0 if passed else 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return run_self_test()
    parser.print_help()
    return 0


if __name__ == "__main__":
    sys.exit(main())
