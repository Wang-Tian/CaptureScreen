#include <iostream>
#include <Windows.h>
#include <GdiPlus.h> // 保存图片用到了GDI+
#include <windowsx.h>
#include <atlbase.h> // 字符串转换用到
#include <string>
#include <functional>
#include <vector>
#include <memory>
#include <dwmapi.h>

#pragma comment(lib, "gdiplus.lib") // 保存图片需要
#pragma comment(lib, "Dwmapi.lib")  // 判断是否是隐形窗口以及获取窗口大小会用到

// 为了将屏幕和窗口进行统一,因此使用了结构体
struct WindowInfo
{
    HWND hwnd; /* 为空表示屏幕截图 */
    std::string desc; // 窗口标题
    RECT rect{ 0,0,0,0 }; /* hwnd不为空时,此参数无效 */
    void* tempPointer = nullptr;
};

namespace GdiplusUtil
{
    static int GetEncoderClsid(const WCHAR* format, CLSID* pClsid)
    {
        UINT  num = 0;          // number of image encoders
        UINT  size = 0;         // size of the image encoder array in bytes

        Gdiplus::ImageCodecInfo* pImageCodecInfo = NULL;

        Gdiplus::GetImageEncodersSize(&num, &size);
        if (size == 0)
            return -1;  // Failure

        pImageCodecInfo = (Gdiplus::ImageCodecInfo*)(malloc(size));
        if (pImageCodecInfo == NULL)
            return -1;  // Failure

        Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);

        for (UINT j = 0; j < num; ++j) {
            if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
                *pClsid = pImageCodecInfo[j].Clsid;
                free(pImageCodecInfo);
                return j;  // Success
            }
        }

        free(pImageCodecInfo);
        return -1;  // Failure
    }

    // 将bitmap对象保存为png图片
    static bool SaveBitmapAsPng(const std::shared_ptr<Gdiplus::Bitmap>& bitmap, const std::string& filename)
    {
        if (bitmap == nullptr) return false;
        CLSID png_clsid;
        GetEncoderClsid(L"image/png", &png_clsid);
        Gdiplus::Status ok = bitmap->Save(CA2T(filename.c_str(), CP_ACP), &png_clsid, nullptr);
        return ok == Gdiplus::Status::Ok;
    }
}

class WindowCapture
{
public:
    using BitmapPtr = std::shared_ptr<Gdiplus::Bitmap>;

    static BitmapPtr Capture(const WindowInfo& wnd_info)
    {
        HDC hWndDC = GetWindowDC(wnd_info.hwnd);
        RECT capture_rect{ 0,0,0,0 }; // 最终要截取的区域
        RECT wnd_rect; // 窗口区域
        RECT real_rect; // 真实的窗口区域,实际上也不是百分百准确

        if (wnd_info.hwnd) {
            ::GetWindowRect(wnd_info.hwnd, &wnd_rect);
            DwmGetWindowAttribute(wnd_info.hwnd, DWMWINDOWATTRIBUTE::DWMWA_EXTENDED_FRAME_BOUNDS, &real_rect, sizeof(RECT));
            int offset_left = real_rect.left - wnd_rect.left;
            int offset_top = real_rect.top - wnd_rect.top;
            capture_rect = RECT{ offset_left,offset_top,real_rect.right - real_rect.left + offset_left,real_rect.bottom - real_rect.top + offset_top };
        }
        else {
            capture_rect = wnd_info.rect;
        }

        int width = capture_rect.right - capture_rect.left;
        int height = capture_rect.bottom - capture_rect.top;

        HDC hMemDC = CreateCompatibleDC(hWndDC);
        HBITMAP hBitmap = CreateCompatibleBitmap(hWndDC, width, height);
        SelectObject(hMemDC, hBitmap);

        BitmapPtr bitmap;
        // 获取指定区域的rgb数据
        bool ok = BitBlt(hMemDC, 0, 0, width, height, hWndDC, capture_rect.left, capture_rect.top, SRCCOPY);
        // hBitmap就是得到的图片对象,转GDI的Bitmap进行保存
        if (ok) bitmap = std::make_shared<Gdiplus::Bitmap>(hBitmap, nullptr);

        DeleteDC(hWndDC);
        DeleteDC(hMemDC);
        DeleteObject(hBitmap);

        return bitmap;
    }
};

class Enumerator
{
public:
    using EnumCallback = std::function<void(const WindowInfo&)>;

    static bool EnumMonitor(EnumCallback callback)
    {
        // 调用Win32Api进行显示器遍历
        return ::EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)&callback);
    }

    static bool EnumWindow(EnumCallback callback)
    {
        // 调用Win32Api进行窗口遍历
        return ::EnumWindows(EnumWindowsProc, (LPARAM)&callback);
    }

private:
    static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
    {
        //::GetParent获取的有可能是所有者窗口,因此使用GetAncestor获取父窗口句柄
        HWND parent = ::GetAncestor(hwnd, GA_PARENT);
        HWND desktop = ::GetDesktopWindow(); // 获取桌面的句柄
        TCHAR szTitle[MAX_PATH] = { 0 };
        ::GetWindowText(hwnd, szTitle, MAX_PATH); // 获取标题

        // 排除父窗口不是桌面的
        if (parent != nullptr && parent != desktop) return TRUE;

        // 排除标题为空的
        if (wcscmp(szTitle, L"") == 0) return TRUE;

        // 排除最小化窗口(因为获取最小化窗口的区域数据是不对的,因此也没办法进行截图等操作)
        if (::IsIconic(hwnd)) return TRUE;

        // 排除不可见窗口,被其他窗口遮挡的情况是可见的
        if (!::IsWindowVisible(hwnd)) return TRUE;

        // 排除对用户隐形的窗口,参考[https://docs.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute]
        DWORD flag = 0;
        DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &flag, sizeof(flag));
        if (flag) return TRUE;

        if (lParam) {
            WindowInfo wnd_info{ hwnd,(LPCSTR)CT2A(szTitle, CP_ACP) };
            EnumCallback* callback_ptr = reinterpret_cast<EnumCallback*>(lParam);
            callback_ptr->operator()(wnd_info);
        }
        return TRUE;
    }

    static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData)
    {
        MONITORINFOEX mi;
        mi.cbSize = sizeof(MONITORINFOEX);
        GetMonitorInfo(hMonitor, &mi);
        if (dwData) {
            std::string device_name = (LPCSTR)CT2A(mi.szDevice, CP_ACP);
            if (mi.dwFlags == MONITORINFOF_PRIMARY) device_name += "(Primary)"; // 主显示器,可根据需要进行操作
            WindowInfo wnd_info{ nullptr, device_name, mi.rcMonitor };

            EnumCallback* callback = reinterpret_cast<EnumCallback*>(dwData);
            (*callback)(wnd_info);
        }
        return TRUE;
    }
};

namespace TestCase
{
    void Run()
    {
        std::vector<WindowInfo> window_vec; // 用来保存窗口信息
        // 枚举显示器
        Enumerator::EnumMonitor([&window_vec](const WindowInfo& wnd_info)
            {
                window_vec.push_back(wnd_info);
            });
        // 计算生成所有屏幕加在一起的区域大小
        if (window_vec.size() > 0) { // 也可大于1,这样只有一个显示器时不会显示全屏选项
            int width = 0, height = 0;
            for (const auto& wnd_info : window_vec) {
                width += wnd_info.rect.right - wnd_info.rect.left;
                int h = wnd_info.rect.bottom - wnd_info.rect.top;
                if (h > height) height = h; // 高度可能不一样,需要以最高的为准
            }
            WindowInfo wnd_info{ nullptr, "FullScreen", { 0, 0, width, height} };
            window_vec.push_back(wnd_info);
        }
        // 枚举窗口
        Enumerator::EnumWindow([&window_vec](const WindowInfo& wnd_info)
            {
                window_vec.push_back(wnd_info);
            });
        // 示例: 遍历找到的所有窗口,将每一个都截图到指定路径,文件夹需存在,程序不会自己创建文件夹
        int cnt = 1;

        for (const auto& window : window_vec) {
            printf("%2d. %s\n", cnt, window.desc.c_str());


            

            auto bitmap = WindowCapture::Capture(window);
            if (bitmap) GdiplusUtil::SaveBitmapAsPng(bitmap, std::to_string(cnt) + ".png");
            ++cnt;
        }
    }
}

int main()
{
    /************************ GDI+ 初始化 ***************************/
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR token;
    Gdiplus::GdiplusStartup(&token, &gdiplusStartupInput, NULL);
    /***********************************************************/

    TestCase::Run();

    Gdiplus::GdiplusShutdown(token); // 关闭GDI
    return 0;
}
