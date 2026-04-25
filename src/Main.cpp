
#include <windows.h>
#include <ddraw.h>

LRESULT CALLBACK DemoWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_CLOSE || message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

int  WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow); int main(int argc, char** argv) { return FreeApiRunWinMain(&WinMain, argc, argv); } int  WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSA windowClass{};
    windowClass.lpfnWndProc = DemoWindowProc;
    windowClass.lpszClassName = "FreeDirectDemoWindow";
    if (RegisterClassA(&windowClass) == 0) {
        return 1;
    }

    HWND window = CreateWindowExA(0,
                                  "FreeDirectDemoWindow",
                                  "Free Direct - DirectDraw 2D Demo",
                                  WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                  100,
                                  100,
                                  800,
                                  600,
                                  NULL,
                                  NULL,
                                  hInstance,
                                  NULL);
    if (!window) {
        return 1;
    }

    ShowWindow(window, nCmdShow);
    UpdateWindow(window);

    LPDIRECTDRAW directDraw = nullptr;
    if (FAILED(DirectDrawCreate(NULL, &directDraw, nullptr))) {
        DestroyWindow(window);
        return 1;
    }

    if (FAILED(directDraw->SetCooperativeLevel(window, 0))) {
        directDraw->Release();
        DestroyWindow(window);
        return 1;
    }

    DDSURFACEDESC surfaceDesc{};
    surfaceDesc.dwSize = sizeof(DDSURFACEDESC);
    surfaceDesc.dwFlags = DDSD_CAPS;
    surfaceDesc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

    LPDIRECTDRAWSURFACE primarySurface = nullptr;
    if (FAILED(directDraw->CreateSurface(&surfaceDesc, &primarySurface, nullptr))) {
        directDraw->Release();
        DestroyWindow(window);
        return 1;
    }

    surfaceDesc = {};
    surfaceDesc.dwSize = sizeof(DDSURFACEDESC);
    surfaceDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    surfaceDesc.dwWidth = 800;
    surfaceDesc.dwHeight = 600;
    surfaceDesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;

    LPDIRECTDRAWSURFACE backSurface = nullptr;
    if (FAILED(directDraw->CreateSurface(&surfaceDesc, &backSurface, nullptr))) {
        primarySurface->Release();
        directDraw->Release();
        DestroyWindow(window);
        return 1;
    }

    bool running = true;
    MSG msg{};
    int frame = 0;
    while (running) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (!running) {
            break;
        }

        DDBLTFX clearFx{};
        clearFx.dwSize = sizeof(DDBLTFX);
        clearFx.dwFillColor = 0x001A1A3A;
        backSurface->Blt(NULL, NULL, NULL, DDBLT_COLORFILL, &clearFx);

        RECT movingRect{};
        movingRect.left = 80 + (frame % 320);
        movingRect.top = 180;
        movingRect.right = movingRect.left + 160;
        movingRect.bottom = movingRect.top + 120;

        DDBLTFX rectFx{};
        rectFx.dwSize = sizeof(DDBLTFX);
        rectFx.dwFillColor = 0x00D89224;
        backSurface->Blt(&movingRect, NULL, NULL, DDBLT_COLORFILL, &rectFx);

        primarySurface->Blt(NULL, backSurface, NULL, DDBLT_WAIT, NULL);

        ++frame;
        Sleep(16);
    }

    backSurface->Release();
    primarySurface->Release();
    directDraw->Release();
    DestroyWindow(window);
    return 0;
}
