# File: .gemini/scripts/take_screenshot.py
import os
import sys
import ctypes
from ctypes import wintypes
from PIL import Image

def switch_to_default_desktop():
    user32 = ctypes.windll.user32
    h_default = user32.OpenDesktopW("Default", 0, False, 0x01FF)
    if h_default:
        user32.SetThreadDesktop(h_default)
    return h_default

def find_unreal_window():
    user32 = ctypes.windll.user32
    hwnds = []
    def enum_cb(hwnd, lparam):
        if user32.IsWindowVisible(hwnd):
            length = user32.GetWindowTextLengthW(hwnd)
            if length > 0:
                buff = ctypes.create_unicode_buffer(length + 1)
                user32.GetWindowTextW(hwnd, buff, length + 1)
                title = buff.value
                if "Unreal Editor" in title or "TDGame" in title:
                    hwnds.append((hwnd, title))
        return True

    WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)
    user32.EnumWindows(WNDENUMPROC(enum_cb), 0)
    return hwnds[0] if hwnds else (None, None)

def capture_rect_from_desktop(left, top, width, height, output_path):
    user32 = ctypes.windll.user32
    gdi32 = ctypes.windll.gdi32

    hdesktop = user32.GetDesktopWindow()
    desktop_dc = user32.GetWindowDC(hdesktop)
    mem_dc = gdi32.CreateCompatibleDC(desktop_dc)
    mem_bitmap = gdi32.CreateCompatibleBitmap(desktop_dc, width, height)
    gdi32.SelectObject(mem_dc, mem_bitmap)

    # SRCCOPY = 0x00CC0020 | CAPTUREBLT = 0x40000000 -> 0x40CC0020
    gdi32.BitBlt(mem_dc, 0, 0, width, height, desktop_dc, left, top, 0x40CC0020)

    class BITMAPINFOHEADER(ctypes.Structure):
        _fields_ = [
            ('biSize', wintypes.DWORD),
            ('biWidth', wintypes.LONG),
            ('biHeight', wintypes.LONG),
            ('biPlanes', wintypes.WORD),
            ('biBitCount', wintypes.WORD),
            ('biCompression', wintypes.DWORD),
            ('biSizeImage', wintypes.DWORD),
            ('biXPelsPerMeter', wintypes.LONG),
            ('biYPelsPerMeter', wintypes.LONG),
            ('biClrUsed', wintypes.DWORD),
            ('biClrImportant', wintypes.DWORD)
        ]

    class BITMAPINFO(ctypes.Structure):
        _fields_ = [
            ('bmiHeader', BITMAPINFOHEADER),
            ('bmiColors', wintypes.DWORD * 3)
        ]

    bmi = BITMAPINFO()
    bmi.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bmi.bmiHeader.biWidth = width
    bmi.bmiHeader.biHeight = -height
    bmi.bmiHeader.biPlanes = 1
    bmi.bmiHeader.biBitCount = 32
    bmi.bmiHeader.biCompression = 0

    buffer_size = width * height * 4
    buffer = ctypes.create_string_buffer(buffer_size)

    gdi32.GetDIBits(desktop_dc, mem_bitmap, 0, height, buffer, ctypes.byref(bmi), 0)

    gdi32.DeleteObject(mem_bitmap)
    gdi32.DeleteDC(mem_dc)
    user32.ReleaseDC(hdesktop, desktop_dc)

    img = Image.frombuffer('RGBA', (width, height), buffer, 'raw', 'BGRA', 0, 1)
    img = img.convert('RGB')
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    img.save(output_path)
    print(f"Captured desktop rect ({left},{top},{width},{height}) to {output_path}")

if __name__ == "__main__":
    switch_to_default_desktop()
    hwnd, title = find_unreal_window()
    if hwnd:
        user32 = ctypes.windll.user32
        rect = wintypes.RECT()
        user32.GetWindowRect(hwnd, ctypes.byref(rect))
        w = rect.right - rect.left
        h = rect.bottom - rect.top
        capture_rect_from_desktop(rect.left, rect.top, w, h, "C:/Project/TDGame/editor_screenshot.png")
    else:
        capture_rect_from_desktop(0, 0, 1920, 1080, "C:/Project/TDGame/editor_screenshot.png")
