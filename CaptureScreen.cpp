#include <iostream>
#include <Windows.h>
#include <GdiPlus.h> // ����ͼƬ�õ���GDI+
#include <windowsx.h>
#include <atlbase.h> // �ַ���ת���õ�
#include <string>
#include <functional>
#include <vector>
#include <memory>
#include <dwmapi.h>

#pragma comment(lib, "gdiplus.lib") // ����ͼƬ��Ҫ
#pragma comment(lib, "Dwmapi.lib")  // �ж��Ƿ������δ����Լ���ȡ���ڴ�С���õ�

// Ϊ�˽���Ļ�ʹ��ڽ���ͳһ,���ʹ���˽ṹ��
struct WindowInfo
{
    HWND hwnd; /* Ϊ�ձ�ʾ��Ļ��ͼ */
    std::string desc; // ���ڱ���
    RECT rect{ 0,0,0,0 }; /* hwnd��Ϊ��ʱ,�˲�����Ч */
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

    // ��bitmap���󱣴�ΪpngͼƬ
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
        RECT capture_rect{ 0,0,0,0 }; // ����Ҫ��ȡ������
        RECT wnd_rect; // ��������
        RECT real_rect; // ��ʵ�Ĵ�������,ʵ����Ҳ���ǰٷְ�׼ȷ

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
        // ��ȡָ�������rgb����
        bool ok = BitBlt(hMemDC, 0, 0, width, height, hWndDC, capture_rect.left, capture_rect.top, SRCCOPY);
        // hBitmap���ǵõ���ͼƬ����,תGDI��Bitmap���б���
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
        // ����Win32Api������ʾ������
        return ::EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)&callback);
    }

    static bool EnumWindow(EnumCallback callback)
    {
        // ����Win32Api���д��ڱ���
        return ::EnumWindows(EnumWindowsProc, (LPARAM)&callback);
    }

private:
    static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
    {
        //::GetParent��ȡ���п����������ߴ���,���ʹ��GetAncestor��ȡ�����ھ��
        HWND parent = ::GetAncestor(hwnd, GA_PARENT);
        HWND desktop = ::GetDesktopWindow(); // ��ȡ����ľ��
        TCHAR szTitle[MAX_PATH] = { 0 };
        ::GetWindowText(hwnd, szTitle, MAX_PATH); // ��ȡ����

        // �ų������ڲ��������
        if (parent != nullptr && parent != desktop) return TRUE;

        // �ų�����Ϊ�յ�
        if (wcscmp(szTitle, L"") == 0) return TRUE;

        // �ų���С������(��Ϊ��ȡ��С�����ڵ����������ǲ��Ե�,���Ҳû�취���н�ͼ�Ȳ���)
        if (::IsIconic(hwnd)) return TRUE;

        // �ų����ɼ�����,�����������ڵ�������ǿɼ���
        if (!::IsWindowVisible(hwnd)) return TRUE;

        // �ų����û����εĴ���,�ο�[https://docs.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute]
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
            if (mi.dwFlags == MONITORINFOF_PRIMARY) device_name += "(Primary)"; // ����ʾ��,�ɸ�����Ҫ���в���
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
        std::vector<WindowInfo> window_vec; // �������洰����Ϣ
        // ö����ʾ��
        Enumerator::EnumMonitor([&window_vec](const WindowInfo& wnd_info)
            {
                window_vec.push_back(wnd_info);
            });
        // ��������������Ļ����һ��������С
        if (window_vec.size() > 0) { // Ҳ�ɴ���1,����ֻ��һ����ʾ��ʱ������ʾȫ��ѡ��
            int width = 0, height = 0;
            for (const auto& wnd_info : window_vec) {
                width += wnd_info.rect.right - wnd_info.rect.left;
                int h = wnd_info.rect.bottom - wnd_info.rect.top;
                if (h > height) height = h; // �߶ȿ��ܲ�һ��,��Ҫ����ߵ�Ϊ׼
            }
            WindowInfo wnd_info{ nullptr, "FullScreen", { 0, 0, width, height} };
            window_vec.push_back(wnd_info);
        }
        // ö�ٴ���
        Enumerator::EnumWindow([&window_vec](const WindowInfo& wnd_info)
            {
                window_vec.push_back(wnd_info);
            });
        // ʾ��: �����ҵ������д���,��ÿһ������ͼ��ָ��·��,�ļ��������,���򲻻��Լ������ļ���
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
    /************************ GDI+ ��ʼ�� ***************************/
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR token;
    Gdiplus::GdiplusStartup(&token, &gdiplusStartupInput, NULL);
    /***********************************************************/

    TestCase::Run();

    Gdiplus::GdiplusShutdown(token); // �ر�GDI
    return 0;
}
