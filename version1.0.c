#define _USE_MATH_DEFINES  // 为了使用 M_PI
#include <stdio.h>
#include <windows.h>
#include <shellapi.h>
#include <string.h>
#include <dwmapi.h>
#include <math.h>
#include <highlevelmonitorconfigurationapi.h>
#include <physicalmonitorenumerationapi.h>
#include <initguid.h>  // 添加这个
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "Dxva2.lib")  // 用于亮度控制
#pragma comment(lib, "winmm.lib")  // 用于音量控制
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#pragma comment(lib, "Ole32.lib")

// 如果仍然找不到 M_PI，手动定义它
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// 调整弹性缓动函数，增加阻尼
#define EASE_OUT_ELASTIC(x) ((x == 0) ? 0 : (x == 1) ? 1 : pow(2, -9 * x) * sin((x - 0.05) * (2 * M_PI) / 0.5) * 0.4 + 1)

// 按钮结构体
typedef struct {
    int x;          // 按钮中心x坐标
    int y;          // 按钮中心y坐标
    int targetY;    // 按钮最终y坐标
    int radius;     // 按钮半径
    BOOL isHovered; // 鼠标悬停状态
    const wchar_t* text;  // 按钮显示文本
    BOOL isAnimating;  // 是否正在动画中
} Button;

// 函数声明
void InitTrayIcon(HWND hwnd);
void EnableBlur(HWND hwnd);
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void ShowFullscreenWindow(HINSTANCE hInstance);
void DrawNormalButton(HDC hdc, Button* button, int alpha);
void DrawRoundedRect(HDC hdc, int x, int y, int width, int height, int radius);
void SetBrightness(float brightness);
void SetVolume(float volume);
void ToggleMute(void);
float GetCurrentBrightness();
float GetCurrentVolume();
// 全局变量
NOTIFYICONDATA nid;    // 系统托盘图标数据
HWND hwnd;            // 主窗口句柄
HWND hwndFullscreen;  // 全屏窗口句柄
BOOL isFullscreen = FALSE;  // 全屏状态标志
#define HOT_KEY_ID 100  // 热键ID
#define FULLSCREEN_CLASS "FullscreenClass"  // 全屏窗口类名
#define BUTTON_RADIUS 40  // 按钮半径
#define BUTTON_SPACING 100  // 按钮间距
#define ANIMATION_STEPS 60  // 减少动画步数以加快速度
#define FINAL_ALPHA 230  // 最终透明度
#define TIMER_ID 1
#define BUTTON_ANIMATION_DISTANCE 300  // 增加按钮上移距离
#define EASE_OUT_EXPO(x) (x == 1.0 ? 1.0 : 1.0 - pow(2, -10 * x))  // 缓动函数
HCURSOR hArrowCursor;

#define ENTER_ANIMATION_STEPS 48  // 增加进入动画步数提高流畅度
#define EXIT_ANIMATION_STEPS 48   // 增加退出动画步数以提高流畅度
#define ANIMATION_INTERVAL 10     // 减少每步时间间隔以增加帧率

// 定义亮度按钮的状态
BOOL isBrightnessSliderVisible = FALSE;

Button buttons[5];  // 4个媒体控制按钮 + 1个音量控制按钮 + 1个空白按钮

// 动画方向枚举
typedef enum {
    ANIM_IN,
    ANIM_OUT
} AnimationDirection;

// 动画状态结构体
typedef struct {
    int currentStep;
    BOOL isAnimating;
    AnimationDirection direction;
} AnimationState;

AnimationState animState = {0, FALSE, ANIM_IN};

// 前向声明
LRESULT CALLBACK FullscreenProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

// 初始化按钮
void InitButtons(int screenWidth, int screenHeight) {
    int centerY = screenHeight / 2;  // 按钮垂直居中
    int startX = (screenWidth - (BUTTON_SPACING * 4)) / 2;  // 计算第一个按钮的起始x坐标
    
    // 初始化每个按钮的位置和文本
    for (int i = 0; i < 5; i++) {
        buttons[i].x = startX + BUTTON_SPACING * i;
        buttons[i].targetY = centerY;
        buttons[i].y = centerY + BUTTON_ANIMATION_DISTANCE;  // 初始位置在目标位置下方
        buttons[i].radius = BUTTON_RADIUS;
        buttons[i].isHovered = FALSE;
        buttons[i].isAnimating = FALSE;
    }
    
    buttons[0].text = L"◀◀";
    buttons[1].text = L"▶/⏸";
    buttons[2].text = L"▶▶";
    buttons[3].text = L"🔊";
    buttons[4].text = L"🔆";  // 亮度图标
}

// 检查点是否按钮内
int IsPointInButton(int x, int y, Button* button) {
    int dx = x - button->x;
    int dy = y - button->y;
    return (dx * dx + dy * dy) <= (button->radius * button->radius);
}

// 开始进入动画
void StartEnterAnimation() {
    animState.currentStep = 0;
    animState.isAnimating = TRUE;
    animState.direction = ANIM_IN;
    SetTimer(hwndFullscreen, TIMER_ID, ANIMATION_INTERVAL, NULL);
}

// 开始退出动画
void StartExitAnimation() {
    animState.currentStep = 0;
    animState.isAnimating = TRUE;
    animState.direction = ANIM_OUT;
    SetTimer(hwndFullscreen, TIMER_ID, ANIMATION_INTERVAL, NULL);
}

typedef struct {
    float currentWidth;
    float currentHeight;
    float currentRadius;
    float targetWidth;
    float targetHeight;
    float targetRadius;
    float textAlpha;      // 文字透明度
    float targetTextAlpha;// 目标文字透明度
    float knobAlpha;      // 滑块透明度
    float targetKnobAlpha;// 目标块透明度
    float sliderValue;    // 滑块的值（0.0 到 1.0）
    BOOL isAnimating;
} SliderAnimState;

SliderAnimState brightnessSliderAnim = {
    .currentWidth = BUTTON_RADIUS * 2,
    .currentHeight = BUTTON_RADIUS * 2,
    .currentRadius = BUTTON_RADIUS,
    .targetWidth = BUTTON_RADIUS * 2,
    .targetHeight = BUTTON_RADIUS * 2,
    .targetRadius = BUTTON_RADIUS,
    .textAlpha = 255.0f,
    .targetTextAlpha = 255.0f,
    .knobAlpha = 0.0f,
    .targetKnobAlpha = 0.0f,
    .sliderValue = 0.5f,  // 初始值设为中间
    .isAnimating = FALSE
};

SliderAnimState volumeSliderAnim = {
    .currentWidth = BUTTON_RADIUS * 2,
    .currentHeight = BUTTON_RADIUS * 2,
    .currentRadius = BUTTON_RADIUS,
    .targetWidth = BUTTON_RADIUS * 2,
    .targetHeight = BUTTON_RADIUS * 2,
    .targetRadius = BUTTON_RADIUS,
    .textAlpha = 255.0f,
    .targetTextAlpha = 255.0f,
    .knobAlpha = 0.0f,
    .targetKnobAlpha = 0.0f,
    .sliderValue = 0.5f,  // 初始值设为中间
    .isAnimating = FALSE
};

// 显示全屏界面
void ShowFullscreenWindow(HINSTANCE hInstance) {
    if (isFullscreen) return;
    
    // 加载箭头光标
    hArrowCursor = LoadCursor(NULL, IDC_ARROW);
    
    // 获取屏幕分辨率
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    
    // 使用系统默认的屏幕分辨率
    InitButtons(screenWidth, screenHeight);
    
    // 创建全屏窗口，添加 WS_EX_TOOLWINDOW 标志
    hwndFullscreen = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,  // 添加 WS_EX_TOOLWINDOW
        FULLSCREEN_CLASS,
        NULL,
        WS_POPUP,
        0, 0, screenWidth, screenHeight,
        NULL, NULL, hInstance, NULL
    );
    
    if (!hwndFullscreen) {
        return;
    }
    
    // 设置窗口的光标
    SetClassLongPtr(hwndFullscreen, GCLP_HCURSOR, (LONG_PTR)hArrowCursor);
    
    // 启用璃效果
    EnableBlur(hwndFullscreen);
    
    // 设置初始透明度为0，使用 LWA_ALPHA
    SetLayeredWindowAttributes(hwndFullscreen, 0, 0, LWA_ALPHA);
    
    // 确保窗口在最顶层并获得焦点
    SetWindowPos(hwndFullscreen, HWND_TOPMOST, 
        0, 0, screenWidth, screenHeight, 
        SWP_SHOWWINDOW);  // 移除 SWP_NOACTIVATE
    
    // 设置键盘焦点
    SetFocus(hwndFullscreen);
    
    isFullscreen = TRUE;
    
    // 开始进入动画
    StartEnterAnimation();
    
    // 立即触发一次重绘
    InvalidateRect(hwndFullscreen, NULL, TRUE);
    UpdateWindow(hwndFullscreen);
    
    // 初始化滑块值
    volumeSliderAnim.sliderValue = GetCurrentVolume();
    // brightnessSliderAnim.sliderValue = GetCurrentBrightness();  // 注释掉
}

// 隐藏全屏界面
void HideFullscreenWindow() {
    if (!isFullscreen) return;
    
    // 开始退出动画
    StartExitAnimation();
}

// 发送媒体控制命令
void SendMediaCommand(int command) {
    keybd_event(command, 0, KEYEVENTF_EXTENDEDKEY, 0);
    keybd_event(command, 0, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
    LPSTR lpCmdLine, int nCmdShow) {
    
    // 注册主窗口类
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "MediaControlClass";
    RegisterClassEx(&wc);
    
    // 注册全屏窗口类
    WNDCLASSEX wcFullscreen = {0};
    wcFullscreen.cbSize = sizeof(WNDCLASSEX);
    wcFullscreen.lpfnWndProc = FullscreenProc;
    wcFullscreen.hInstance = hInstance;
    wcFullscreen.hbrBackground = CreateSolidBrush(RGB(0, 0, 0));
    wcFullscreen.lpszClassName = FULLSCREEN_CLASS;
    wcFullscreen.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassEx(&wcFullscreen);
    
    // 创建主窗口
    hwnd = CreateWindowEx(
        0, "MediaControlClass", "媒体控制工具",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
        NULL, NULL, hInstance, NULL
    );
    
    if (hwnd == NULL) return 0;
    
    // 初始化系统托盘图标
    InitTrayIcon(hwnd);
    
    // 使用热键而不是鼠标钩子
    RegisterHotKey(hwnd, HOT_KEY_ID, MOD_ALT, VK_SPACE);
    
    if (hwnd == NULL) return 0;
    
    // 隐藏主窗口
    ShowWindow(hwnd, SW_HIDE);
    
    // 消息循环
    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    // 清理（删除钩子相关代码）
    UnregisterHotKey(hwnd, HOT_KEY_ID);
    Shell_NotifyIcon(NIM_DELETE, &nid);
    
    return 0;
}

// 添加音量滑动条的状态
BOOL isVolumeSliderVisible = FALSE;

//  SliderAnimState 结体添加滑块透明度
/*typedef struct {
    float currentWidth;
    float currentHeight;
    float currentRadius;
    float targetWidth;
    float targetHeight;
    float targetRadius;
    float textAlpha;      // 文字透明度
    float targetTextAlpha;// 目标文字透明度
    float knobAlpha;      // 滑块透明度
    float targetKnobAlpha;// 目标块透明度
    float sliderValue;    // 滑块的值（0.0 到 1.0）
    BOOL isAnimating;
} SliderAnimState;

// 修改初始化值，添加滑块透明度的初始化
SliderAnimState brightnessSliderAnim = {
    .currentWidth = BUTTON_RADIUS * 2,
    .currentHeight = BUTTON_RADIUS * 2,
    .currentRadius = BUTTON_RADIUS,
    .targetWidth = BUTTON_RADIUS * 2,
    .targetHeight = BUTTON_RADIUS * 2,
    .targetRadius = BUTTON_RADIUS,
    .textAlpha = 255.0f,
    .targetTextAlpha = 255.0f,
    .knobAlpha = 0.0f,
    .targetKnobAlpha = 0.0f,
    .sliderValue = 0.5f,  // 初始值设为中间
    .isAnimating = FALSE
};

SliderAnimState volumeSliderAnim = {
    .currentWidth = BUTTON_RADIUS * 2,
    .currentHeight = BUTTON_RADIUS * 2,
    .currentRadius = BUTTON_RADIUS,
    .targetWidth = BUTTON_RADIUS * 2,
    .targetHeight = BUTTON_RADIUS * 2,
    .targetRadius = BUTTON_RADIUS,
    .textAlpha = 255.0f,
    .targetTextAlpha = 255.0f,
    .knobAlpha = 0.0f,
    .targetKnobAlpha = 0.0f,
    .sliderValue = 0.5f,  // 初始值设为中间
    .isAnimating = FALSE
};
*/
// 修改 DrawButton 函数
void DrawButton(HDC hdc, Button* button, int alpha) {
    if (button->text == L"🔆" || button->text == L"🔊") {
        SliderAnimState* sliderAnim = (button->text == L"🔆") ? &brightnessSliderAnim : &volumeSliderAnim;
        BOOL isSliderVisible = (button->text == L"🔆") ? isBrightnessSliderVisible : isVolumeSliderVisible;
        
        // 绘制按钮背景
        HBRUSH brush = CreateSolidBrush(RGB(60 * alpha / 255, 60 * alpha / 255, 60 * alpha / 255));
        SelectObject(hdc, brush);
        
        // 绘制圆角矩形
        int x = button->x - (int)(sliderAnim->currentWidth / 2);
        int y = button->y - (int)(sliderAnim->currentHeight / 2);
        int radius = min((int)sliderAnim->currentRadius, (int)(sliderAnim->currentWidth / 2));
        DrawRoundedRect(hdc, x, y, 
            (int)sliderAnim->currentWidth, 
            (int)sliderAnim->currentHeight, 
            radius);
        
        // 根据状态决定绘制顺
        if (isSliderVisible || (sliderAnim->isAnimating && sliderAnim->targetTextAlpha < sliderAnim->textAlpha)) {
            // 滑动条状态：先绘制文字（如果有），再绘制滑块
            if (sliderAnim->textAlpha > 0) {
                SetBkMode(hdc, TRANSPARENT);
                
                // 修改文字亮度计算，从背景色过渡到白色
                int textBrightness = (int)(60 + (255 - 60) * (sliderAnim->textAlpha / 255.0f));
                SetTextColor(hdc, RGB(textBrightness * alpha / 255, 
                                    textBrightness * alpha / 255, 
                                    textBrightness * alpha / 255));
                
                HFONT hFont = CreateFontW(30, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"微软雅黑");
                SelectObject(hdc, hFont);
                
                RECT textRect = {
                    button->x - button->radius,
                    button->y - button->radius,
                    button->x + button->radius,
                    button->y + button->radius
                };
                DrawTextW(hdc, button->text, -1, &textRect, 
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                
                DeleteObject(hFont);
            }
            
            // 绘制滑块
            if (sliderAnim->currentHeight > sliderAnim->currentWidth) {
                int sliderHeight = (int)sliderAnim->currentHeight - (int)sliderAnim->currentRadius * 2;
                int baseY = y + (int)sliderAnim->currentRadius;
                int knobY = baseY + (int)(sliderHeight * (1.0f - sliderAnim->sliderValue));
                int knobRadius = (int)(sliderAnim->currentRadius * 0.8f);
                
                int knobBrightness = (int)(60 + (255 - 60) * (sliderAnim->knobAlpha / 255.0f));
                HBRUSH whiteBrush = CreateSolidBrush(RGB(
                    knobBrightness * alpha / 255,
                    knobBrightness * alpha / 255,
                    knobBrightness * alpha / 255));
                SelectObject(hdc, whiteBrush);
                
                HRGN knobRegion = CreateEllipticRgn(
                    button->x - knobRadius,
                    knobY - knobRadius,
                    button->x + knobRadius,
                    knobY + knobRadius
                );
                FillRgn(hdc, knobRegion, whiteBrush);
                
                DeleteObject(knobRegion);
                DeleteObject(whiteBrush);
            }
        } else {
            // 按钮状态：先绘制滑块（如果有），再绘制文字
            if (sliderAnim->currentHeight > sliderAnim->currentWidth && sliderAnim->knobAlpha > 0) {
                int sliderHeight = (int)sliderAnim->currentHeight - (int)sliderAnim->currentRadius * 2;
                int baseY = y + (int)sliderAnim->currentRadius;
                int knobY = baseY + (int)(sliderHeight * (1.0f - sliderAnim->sliderValue));
                int knobRadius = (int)(sliderAnim->currentRadius * 0.8f);
                
                int knobBrightness = (int)(60 + (255 - 60) * (sliderAnim->knobAlpha / 255.0f));
                HBRUSH whiteBrush = CreateSolidBrush(RGB(
                    knobBrightness * alpha / 255,
                    knobBrightness * alpha / 255,
                    knobBrightness * alpha / 255));
                SelectObject(hdc, whiteBrush);
                
                HRGN knobRegion = CreateEllipticRgn(
                    button->x - knobRadius,
                    knobY - knobRadius,
                    button->x + knobRadius,
                    knobY + knobRadius
                );
                FillRgn(hdc, knobRegion, whiteBrush);
                
                DeleteObject(knobRegion);
                DeleteObject(whiteBrush);
            }
            
            if (sliderAnim->textAlpha > 0) {
                SetBkMode(hdc, TRANSPARENT);
                
                int textBrightness = (int)(60 + (255 - 60) * (sliderAnim->textAlpha / 255.0f));
                SetTextColor(hdc, RGB(textBrightness * alpha / 255, 
                                    textBrightness * alpha / 255, 
                                    textBrightness * alpha / 255));
                
                HFONT hFont = CreateFontW(30, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"微软雅黑");
                SelectObject(hdc, hFont);
                
                RECT textRect = {
                    button->x - button->radius,
                    button->y - button->radius,
                    button->x + button->radius,
                    button->y + button->radius
                };
                DrawTextW(hdc, button->text, -1, &textRect, 
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                
                DeleteObject(hFont);
            }
        }
        
        DeleteObject(brush);
    } else {
        DrawNormalButton(hdc, button, alpha);
    }
}

// 修改 DrawNormalButton 函数
void DrawNormalButton(HDC hdc, Button* button, int alpha) {
    // 移除悬停状态的颜色变化，始终使用相同的颜色
    HBRUSH brush = CreateSolidBrush(RGB(60 * alpha / 255, 60 * alpha / 255, 60 * alpha / 255));
    SelectObject(hdc, brush);
    
    Ellipse(hdc, 
        button->x - button->radius,
        button->y - button->radius,
        button->x + button->radius,
        button->y + button->radius);
    
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255 * alpha / 255, 255 * alpha / 255, 255 * alpha / 255));
    
    HFONT hFont = CreateFontW(30, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"微软雅黑");
    SelectObject(hdc, hFont);
    
    RECT textRect = {
        button->x - button->radius,
        button->y - button->radius,
        button->x + button->radius,
        button->y + button->radius
    };
    DrawTextW(hdc, button->text, -1, &textRect, 
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    
    DeleteObject(brush);
    DeleteObject(hFont);
}

// 全屏窗口过程
LRESULT CALLBACK FullscreenProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static HDC hdcBuffer = NULL;
    static HBITMAP hBitmap = NULL;
    static HBITMAP hOldBitmap = NULL;
    
    switch (uMsg) {
        case WM_CREATE: {
            RECT rect;
            GetClientRect(hwnd, &rect);
            HDC hdc = GetDC(hwnd);
            
            // 创建持久的缓冲区
            hdcBuffer = CreateCompatibleDC(hdc);
            hBitmap = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
            hOldBitmap = (HBITMAP)SelectObject(hdcBuffer, hBitmap);
            
            ReleaseDC(hwnd, hdc);
            return 0;
        }
        
        case WM_DESTROY:
            if (hdcBuffer) {
                SelectObject(hdcBuffer, hOldBitmap);
                DeleteDC(hdcBuffer);
                DeleteObject(hBitmap);
            }
            return 0;
            
        case WM_ERASEBKGND:
            return 1;  // 防止背景擦除导致闪烁
            
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            // 使用固定的透明度值
            int alpha = FINAL_ALPHA;
            
            // 使用持久的缓冲区绘制
            RECT rect;
            GetClientRect(hwnd, &rect);
            
            // 清除缓冲区
            HBRUSH hBrush = CreateSolidBrush(RGB(0, 0, 0));
            FillRect(hdcBuffer, &rect, hBrush);
            DeleteObject(hBrush);
            
            // 绘制按钮到缓冲区
            for (int i = 0; i < 5; i++) {
                DrawButton(hdcBuffer, &buttons[i], alpha);
            }
            
            // 复制到屏幕
            BitBlt(hdc, 0, 0, rect.right, rect.bottom, hdcBuffer, 0, 0, SRCCOPY);
            
            EndPaint(hwnd, &ps);
            return 0;
        }
        
        case WM_MOUSEMOVE: {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            BOOL needRedraw = FALSE;
            
            for (int i = 0; i < 5; i++) {
                BOOL newHovered = IsPointInButton(x, y, &buttons[i]);
                if (newHovered != buttons[i].isHovered) {
                    buttons[i].isHovered = newHovered;
                    needRedraw = TRUE;
                    
                    if (i == 4 || i == 3) {  // 亮度或音量按钮
                        SliderAnimState* sliderAnim = (i == 4) ? &brightnessSliderAnim : &volumeSliderAnim;
                        BOOL* isVisible = (i == 4) ? &isBrightnessSliderVisible : &isVolumeSliderVisible;
                        
                        if (newHovered != *isVisible) {
                            *isVisible = newHovered;
                            if (newHovered) {
                                // 展开为滑动条
                                sliderAnim->targetWidth = BUTTON_RADIUS;
                                sliderAnim->targetHeight = BUTTON_RADIUS * 6;
                                sliderAnim->targetRadius = BUTTON_RADIUS / 2;
                                sliderAnim->targetTextAlpha = 0;     // 文字淡出
                                sliderAnim->targetKnobAlpha = 255;   // 滑块淡入
                            } else {
                                // 收缩为按钮
                                sliderAnim->targetWidth = BUTTON_RADIUS * 2;
                                sliderAnim->targetHeight = BUTTON_RADIUS * 2;
                                sliderAnim->targetRadius = BUTTON_RADIUS;
                                sliderAnim->targetTextAlpha = 255;   // 文字淡入
                                sliderAnim->targetKnobAlpha = 0;     // 滑块淡出
                            }
                            sliderAnim->isAnimating = TRUE;
                            SetTimer(hwnd, (i == 4) ? 2 : 4, 16, NULL);
                        }
                    }
                }
            }
            
            if (needRedraw) {
                InvalidateRect(hwnd, NULL, FALSE);
                UpdateWindow(hwnd);
            }
            return 0;
        }
        
        case WM_LBUTTONUP: {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            BOOL buttonClicked = FALSE;
            
            // 检查是否点击了按钮
            for (int i = 0; i < 5; i++) {
                if (IsPointInButton(x, y, &buttons[i])) {
                    switch (i) {
                        case 0: SendMediaCommand(VK_MEDIA_PREV_TRACK); break;
                        case 1: SendMediaCommand(VK_MEDIA_PLAY_PAUSE); break;
                        case 2: SendMediaCommand(VK_MEDIA_NEXT_TRACK); break;
                        case 3: ToggleMute(); break;  // 音量按钮点击切换静音
                        case 4: /* 亮度按钮不再需要点击处理 */ break;
                    }
                    buttonClicked = TRUE;
                    break;
                }
            }
            
            // 如果点击了空白区域，则隐藏窗口
            if (!buttonClicked) {
                HideFullscreenWindow();
            }
            return 0;
        }
        
        case WM_RBUTTONUP:
            HideFullscreenWindow();
            return 0;
            
        case WM_SETCURSOR:
            SetCursor(hArrowCursor);
            return TRUE;
            
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                PostMessage(hwnd, WM_CLOSE, 0, 0);  // 使用 PostMessage 而不是直接调用 HideFullscreenWindow
                return 0;
            }
            break;
            
        case WM_TIMER: {
            if (wParam == TIMER_ID && animState.isAnimating) {
                animState.currentStep++;
                
                // 根据动画方向选择不同的步数
                int totalSteps = (animState.direction == ANIM_IN) ? ENTER_ANIMATION_STEPS : EXIT_ANIMATION_STEPS;
                
                // 算动画进（0.0 到 1.0）
                double progress = (double)animState.currentStep / totalSteps;
                double easedProgress = (animState.direction == ANIM_IN) ? EASE_OUT_ELASTIC(progress) : EASE_OUT_EXPO(progress);
                
                // 更新背景透明度，使用非线性式
                int backgroundAlpha;
                if (animState.direction == ANIM_IN) {
                    backgroundAlpha = (int)(EASE_OUT_EXPO(progress) * FINAL_ALPHA);
                } else {
                    backgroundAlpha = (int)((1.0 - EASE_OUT_EXPO(progress)) * FINAL_ALPHA);
                }
                SetLayeredWindowAttributes(hwnd, 0, backgroundAlpha, LWA_ALPHA);
                
                // 更新按钮位置
                for (int i = 0; i < 5; i++) {
                    int distance;
                    if (animState.direction == ANIM_IN) {
                        distance = (int)((1.0 - easedProgress) * BUTTON_ANIMATION_DISTANCE);
                        buttons[i].y = buttons[i].targetY + distance;  // 从下向上移动
                    } else {
                        distance = (int)(easedProgress * BUTTON_ANIMATION_DISTANCE);
                        buttons[i].y = buttons[i].targetY + distance;  // 向下移动
                    }
                }
                
                // 强制重绘
                InvalidateRect(hwnd, NULL, FALSE);  // 将最后一个参数改为 FALSE 以减少闪烁
                UpdateWindow(hwnd);
                
                // 检查动画是否完成
                if (animState.currentStep >= totalSteps) {
                    KillTimer(hwnd, TIMER_ID);
                    animState.isAnimating = FALSE;
                    
                    // 如果是退出动画，则销毁窗口
                    if (animState.direction == ANIM_OUT) {
                        DestroyWindow(hwnd);
                        isFullscreen = FALSE;
                    }
                }
            } else if (wParam == 2 && brightnessSliderAnim.isAnimating) {
                float speedW = (brightnessSliderAnim.targetWidth - brightnessSliderAnim.currentWidth) * 0.3f;  // 加快速度
                float speedH = (brightnessSliderAnim.targetHeight - brightnessSliderAnim.currentHeight) * 0.3f;
                float speedR = (brightnessSliderAnim.targetRadius - brightnessSliderAnim.currentRadius) * 0.3f;
                float speedA = (brightnessSliderAnim.targetTextAlpha - brightnessSliderAnim.textAlpha) * 0.3f;
                float speedKnob = (brightnessSliderAnim.targetKnobAlpha - brightnessSliderAnim.knobAlpha) * 0.3f;
                
                brightnessSliderAnim.currentWidth += speedW;
                brightnessSliderAnim.currentHeight += speedH;
                brightnessSliderAnim.currentRadius += speedR;
                brightnessSliderAnim.textAlpha += speedA;
                brightnessSliderAnim.knobAlpha += speedKnob;
                
                // 检查动画是否完成
                if (abs(speedW) < 0.1f && abs(speedH) < 0.1f && abs(speedR) < 0.1f && abs(speedA) < 0.1f && abs(speedKnob) < 0.1f) {
                    brightnessSliderAnim.currentWidth = brightnessSliderAnim.targetWidth;
                    brightnessSliderAnim.currentHeight = brightnessSliderAnim.targetHeight;
                    brightnessSliderAnim.currentRadius = brightnessSliderAnim.targetRadius;
                    brightnessSliderAnim.textAlpha = brightnessSliderAnim.targetTextAlpha;
                    brightnessSliderAnim.knobAlpha = brightnessSliderAnim.targetKnobAlpha;
                    brightnessSliderAnim.isAnimating = FALSE;
                    KillTimer(hwnd, 2);
                }
                
                InvalidateRect(hwnd, NULL, FALSE);
                UpdateWindow(hwnd);
            } else if (wParam == 4 && volumeSliderAnim.isAnimating) {
                float speedW = (volumeSliderAnim.targetWidth - volumeSliderAnim.currentWidth) * 0.3f;
                float speedH = (volumeSliderAnim.targetHeight - volumeSliderAnim.currentHeight) * 0.3f;
                float speedR = (volumeSliderAnim.targetRadius - volumeSliderAnim.currentRadius) * 0.3f;
                float speedA = (volumeSliderAnim.targetTextAlpha - volumeSliderAnim.textAlpha) * 0.3f;
                float speedKnob = (volumeSliderAnim.targetKnobAlpha - volumeSliderAnim.knobAlpha) * 0.3f;
                
                volumeSliderAnim.currentWidth += speedW;
                volumeSliderAnim.currentHeight += speedH;
                volumeSliderAnim.currentRadius += speedR;
                volumeSliderAnim.textAlpha += speedA;
                volumeSliderAnim.knobAlpha += speedKnob;
                
                // 检查动画是否完成
                if (abs(speedW) < 0.1f && abs(speedH) < 0.1f && abs(speedR) < 0.1f && abs(speedA) < 0.1f && abs(speedKnob) < 0.1f) {
                    volumeSliderAnim.currentWidth = volumeSliderAnim.targetWidth;
                    volumeSliderAnim.currentHeight = volumeSliderAnim.targetHeight;
                    volumeSliderAnim.currentRadius = volumeSliderAnim.targetRadius;
                    volumeSliderAnim.textAlpha = volumeSliderAnim.targetTextAlpha;
                    volumeSliderAnim.knobAlpha = volumeSliderAnim.targetKnobAlpha;
                    volumeSliderAnim.isAnimating = FALSE;
                    KillTimer(hwnd, 4);
                }
                
                InvalidateRect(hwnd, NULL, FALSE);
                UpdateWindow(hwnd);
            }
            return 0;
        }
        
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) {
                // 当窗口失去焦点时，确保它仍然可以接收键盘输入
                SetFocus(hwnd);
                return 0;
            }
            break;
            
        case WM_CLOSE:
            HideFullscreenWindow();
            return 0;
            
        case WM_MOUSEWHEEL: {
            if (isBrightnessSliderVisible || isVolumeSliderVisible) {
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                float step = 0.05f;  // 每次改变5%
                
                POINT pt;
                pt.x = LOWORD(lParam);
                pt.y = HIWORD(lParam);
                ScreenToClient(hwnd, &pt);
                
                if (isBrightnessSliderVisible && IsPointInButton(pt.x, pt.y, &buttons[4])) {
                    // 注释掉亮度控制部分
                    /*if (delta > 0) {
                        brightnessSliderAnim.sliderValue = min(1.0f, brightnessSliderAnim.sliderValue + step);
                    } else {
                        brightnessSliderAnim.sliderValue = max(0.0f, brightnessSliderAnim.sliderValue - step);
                    }
                    SetBrightness(brightnessSliderAnim.sliderValue);*/
                    InvalidateRect(hwnd, NULL, FALSE);
                    UpdateWindow(hwnd);
                } else if (isVolumeSliderVisible && IsPointInButton(pt.x, pt.y, &buttons[3])) {
                    // 保持音量控制部分不变
                    if (delta > 0) {
                        volumeSliderAnim.sliderValue = min(1.0f, volumeSliderAnim.sliderValue + step);
                    } else {
                        volumeSliderAnim.sliderValue = max(0.0f, volumeSliderAnim.sliderValue - step);
                    }
                    SetVolume(volumeSliderAnim.sliderValue);  // 直接设置音量百分比
                    InvalidateRect(hwnd, NULL, FALSE);
                    UpdateWindow(hwnd);
                }
            }
            return 0;
        }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_HOTKEY:
            if (wParam == HOT_KEY_ID) {
                // 确保在主线程中执行
                PostMessage(hwnd, WM_COMMAND, 1, 0);  // 使用已有的"显示"命令
            }
            return 0;
            
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
            
        case WM_USER + 1:  // 系统托盘消息
            if (lParam == WM_RBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, 1, L"显示");
                AppendMenuW(hMenu, MF_STRING, 2, L"退出");
                SetForegroundWindow(hwnd);
                TrackPopupMenu(hMenu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN,
                    pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);
            }
            return 0;
            
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case 1:  // "显示"按钮
                    ShowFullscreenWindow(GetModuleHandle(NULL));
                    return 0;
                case 2:  // "退出"按钮
                    DestroyWindow(hwnd);
                    return 0;
            }
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// 定义多体按键常量（如未定义）
#ifndef VK_MEDIA_NEXT_TRACK
#define VK_MEDIA_NEXT_TRACK 0xB0
#endif

#ifndef VK_MEDIA_PREV_TRACK
#define VK_MEDIA_PREV_TRACK 0xB1
#endif

#ifndef VK_MEDIA_PLAY_PAUSE
#define VK_MEDIA_PLAY_PAUSE 0xB3
#endif

#ifndef VK_VOLUME_MUTE
#define VK_VOLUME_MUTE 0xAD
#endif

// 初始化系统托盘图标
void InitTrayIcon(HWND hwnd) {
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_USER + 1;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    strcpy(nid.szTip, "媒体控制工具");  // 使用 ANSI 字符串
    Shell_NotifyIcon(NIM_ADD, &nid);
}

// 添加毛玻璃效果函数
void EnableBlur(HWND hwnd) {
    HMODULE hUser = GetModuleHandle(TEXT("user32.dll"));
    if (hUser) {
        struct ACCENTPOLICY {
            int nAccentState;
            int nFlags;
            int nColor;
            int nAnimationId;
        };
        struct WINCOMPATTRDATA {
            int nAttribute;
            PVOID pData;
            ULONG ulDataSize;
        };
        
        typedef BOOL(WINAPI*pSetWindowCompositionAttribute)(HWND, struct WINCOMPATTRDATA*);
        const pSetWindowCompositionAttribute SetWindowCompositionAttribute = 
            (pSetWindowCompositionAttribute)GetProcAddress(hUser, "SetWindowCompositionAttribute");
        
        if (SetWindowCompositionAttribute) {
            struct ACCENTPOLICY policy;
            policy.nAccentState = 3;  // ACCENT_ENABLE_BLURBEHIND
            policy.nFlags = 0;
            policy.nColor = 0x80000000;  // 半透明黑色
            policy.nAnimationId = 0;
            
            struct WINCOMPATTRDATA data;
            data.nAttribute = 19;  // WCA_ACCENT_POLICY
            data.pData = &policy;
            data.ulDataSize = sizeof(struct ACCENTPOLICY);
            
            SetWindowCompositionAttribute(hwnd, &data);
        }
    }
}

// 修改 DrawToBuffer 函数
void DrawToBuffer(HDC hdc, HBITMAP hBitmap, int screenWidth, int screenHeight, int alpha) {
    HDC memDC = CreateCompatibleDC(hdc);
    SelectObject(memDC, hBitmap);
    
    // 清除背景
    HBRUSH hBrush = CreateSolidBrush(RGB(0, 0, 0));
    RECT rect = {0, 0, screenWidth, screenHeight};
    FillRect(memDC, &rect, hBrush);
    DeleteObject(hBrush);
    
    // 绘制所有按钮
    for (int i = 0; i < 5; i++) {
        // 移除悬停状态的颜色变化，使用固定颜色
        HBRUSH brush = CreateSolidBrush(RGB(60 * alpha / 255, 60 * alpha / 255, 60 * alpha / 255));
        
        SelectObject(memDC, brush);
        
        Ellipse(memDC, 
            buttons[i].x - buttons[i].radius,
            buttons[i].y - buttons[i].radius,
            buttons[i].x + buttons[i].radius,
            buttons[i].y + buttons[i].radius);
        
        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, RGB(255 * alpha / 255, 255 * alpha / 255, 255 * alpha / 255));
        
        HFONT hFont = CreateFontW(30, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"微软雅黑");
        SelectObject(memDC, hFont);
        
        RECT textRect = {
            buttons[i].x - buttons[i].radius,
            buttons[i].y - buttons[i].radius,
            buttons[i].x + buttons[i].radius,
            buttons[i].y + buttons[i].radius
        };
        DrawTextW(memDC, buttons[i].text, -1, &textRect, 
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        
        DeleteObject(brush);
        DeleteObject(hFont);
    }
    
    // 将缓冲区内容复制到屏幕
    BitBlt(hdc, 0, 0, screenWidth, screenHeight, memDC, 0, 0, SRCCOPY);
    DeleteDC(memDC);
}

// 绘制圆角矩形
void DrawRoundedRect(HDC hdc, int x, int y, int width, int height, int radius) {
    HRGN roundRectRegion = CreateRoundRectRgn(x, y, x + width, y + height, radius * 2, radius * 2);
    HBRUSH brush = (HBRUSH)GetCurrentObject(hdc, OBJ_BRUSH);
    FillRgn(hdc, roundRectRegion, brush);
    DeleteObject(roundRectRegion);
}

// 修改音量控制函数，使用更简单的方法
void SetVolume(float volume) {
    HRESULT hr;
    IMMDeviceEnumerator* pEnumerator = NULL;
    IMMDevice* pDevice = NULL;
    IAudioEndpointVolume* pEndpointVolume = NULL;
    
    // 初始化 COM
    hr = CoInitialize(NULL);
    if (FAILED(hr)) return;
    
    // 创建设备枚举器
    hr = CoCreateInstance(
        &CLSID_MMDeviceEnumerator, NULL,
        CLSCTX_ALL, &IID_IMMDeviceEnumerator,
        (void**)&pEnumerator);
    
    if (SUCCEEDED(hr)) {
        // 获取默认音频设备
        hr = pEnumerator->lpVtbl->GetDefaultAudioEndpoint(
            pEnumerator, eRender, eConsole, &pDevice);
        
        if (SUCCEEDED(hr)) {
            // 获取音量控制接口
            hr = pDevice->lpVtbl->Activate(
                pDevice,
                &IID_IAudioEndpointVolume,
                CLSCTX_ALL,
                NULL,
                (void**)&pEndpointVolume);
            
            if (SUCCEEDED(hr)) {
                // 设置音量（0.0 到 1.0）
                pEndpointVolume->lpVtbl->SetMasterVolumeLevelScalar(
                    pEndpointVolume,
                    volume,
                    NULL);
                
                pEndpointVolume->lpVtbl->Release(pEndpointVolume);
            }
            pDevice->lpVtbl->Release(pDevice);
        }
        pEnumerator->lpVtbl->Release(pEnumerator);
    }
    
    // 清理 COM
    CoUninitialize();
}

// 在文件开头添加键盘事件的常量定义
#ifndef VK_VOLUME_UP
#define VK_VOLUME_UP 0xAF
#endif

#ifndef VK_VOLUME_DOWN
#define VK_VOLUME_DOWN 0xAE
#endif

#ifndef VK_BRIGHTNESS_UP
#define VK_BRIGHTNESS_UP 0x97
#endif

#ifndef VK_BRIGHTNESS_DOWN
#define VK_BRIGHTNESS_DOWN 0x98
#endif

// 添加静音控制函数
void ToggleMute() {
    HRESULT hr;
    IMMDeviceEnumerator* pEnumerator = NULL;
    IMMDevice* pDevice = NULL;
    IAudioEndpointVolume* pEndpointVolume = NULL;
    
    // 初始化 COM
    hr = CoInitialize(NULL);
    if (FAILED(hr)) return;
    
    // 创建设备枚举器
    hr = CoCreateInstance(
        &CLSID_MMDeviceEnumerator, NULL,
        CLSCTX_ALL, &IID_IMMDeviceEnumerator,
        (void**)&pEnumerator);
    
    if (SUCCEEDED(hr)) {
        // 获取默认音频设备
        hr = pEnumerator->lpVtbl->GetDefaultAudioEndpoint(
            pEnumerator, eRender, eConsole, &pDevice);
        
        if (SUCCEEDED(hr)) {
            // 获取音量控制接口
            hr = pDevice->lpVtbl->Activate(
                pDevice,
                &IID_IAudioEndpointVolume,
                CLSCTX_ALL,
                NULL,
                (void**)&pEndpointVolume);
            
            if (SUCCEEDED(hr)) {
                // 获取当前静音状态并切换
                BOOL mute;
                pEndpointVolume->lpVtbl->GetMute(pEndpointVolume, &mute);
                pEndpointVolume->lpVtbl->SetMute(pEndpointVolume, !mute, NULL);
                
                pEndpointVolume->lpVtbl->Release(pEndpointVolume);
            }
            pDevice->lpVtbl->Release(pDevice);
        }
        pEnumerator->lpVtbl->Release(pEnumerator);
    }
    
    // 清理 COM
    CoUninitialize();
}

// 添加获取当前音量的函数
float GetCurrentVolume() {
    float volume = 0.5f;  // 默认值
    HRESULT hr;
    IMMDeviceEnumerator* pEnumerator = NULL;
    IMMDevice* pDevice = NULL;
    IAudioEndpointVolume* pEndpointVolume = NULL;
    
    // 初始化 COM
    hr = CoInitialize(NULL);
    if (FAILED(hr)) return volume;
    
    // 创建设备枚举器
    hr = CoCreateInstance(
        &CLSID_MMDeviceEnumerator, NULL,
        CLSCTX_ALL, &IID_IMMDeviceEnumerator,
        (void**)&pEnumerator);
    
    if (SUCCEEDED(hr)) {
        // 获取默认音频设备
        hr = pEnumerator->lpVtbl->GetDefaultAudioEndpoint(
            pEnumerator, eRender, eConsole, &pDevice);
        
        if (SUCCEEDED(hr)) {
            // 获取音量控制接口
            hr = pDevice->lpVtbl->Activate(
                pDevice,
                &IID_IAudioEndpointVolume,
                CLSCTX_ALL,
                NULL,
                (void**)&pEndpointVolume);
            
            if (SUCCEEDED(hr)) {
                // 获取当前音量
                pEndpointVolume->lpVtbl->GetMasterVolumeLevelScalar(
                    pEndpointVolume,
                    &volume);
                
                pEndpointVolume->lpVtbl->Release(pEndpointVolume);
            }
            pDevice->lpVtbl->Release(pDevice);
        }
        pEnumerator->lpVtbl->Release(pEnumerator);
    }
    
    // 清理 COM
    CoUninitialize();
    return volume;
}

// 添加获取当前亮度的函数
float GetCurrentBrightness() {
    float brightness = 0.5f;  // 默认值
    
    // 创建命令字符串
    char command[256] = "powershell \"(Get-WmiObject -Namespace root/WMI -Class WmiMonitorBrightness).CurrentBrightness\"";
    char buffer[256] = {0};
    
    // 创建管道执行命令
    FILE* pipe = _popen(command, "r");
    if (pipe) {
        // 读取命令输出
        if (fgets(buffer, sizeof(buffer), pipe) != NULL) {
            // 转换为浮点数（0-100）
            int value = atoi(buffer);
            brightness = value / 100.0f;
        }
        _pclose(pipe);
    }
    
    return brightness;
}