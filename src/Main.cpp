
#include <windows.h>
#include <ddraw.h>
#include <cmath>
#include <vector>
#include <SDL3_image/SDL_image.h>
#include <iostream>

LRESULT CALLBACK DemoWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_CLOSE || message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow);
int main(int argc, char** argv) { return FreeApiRunWinMain(&WinMain, argc, argv); }

int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSA windowClass{};
    windowClass.lpfnWndProc = DemoWindowProc;
    windowClass.lpszClassName = "FreeDirectDemoWindow";
    if (RegisterClassA(&windowClass) == 0) {
        return 1;
    }

    HWND window = CreateWindowExA(0,
                                  "FreeDirectDemoWindow",
                                  "Free Direct - Improved DirectDraw Demo",
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

    if (FAILED(directDraw->SetCooperativeLevel(window, DDSCL_NORMAL))) {
        directDraw->Release();
        DestroyWindow(window);
        return 1;
    }

    // Primary Surface
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

    // Back Surface (32-bit)
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

    // Load PNG using SDL_image and copy to a DirectDraw surface
    auto loadSurfaceFromFile = [&](LPDIRECTDRAW dd, const char* filename) -> LPDIRECTDRAWSURFACE {
        SDL_Surface* loaded = IMG_Load(filename);
        if (!loaded) {
            std::cerr << "Failed to load image: " << filename << " error: " << SDL_GetError() << std::endl;
            // Try in cmake-build-debug just in case
            std::string fallback = std::string("cmake-build-debug/") + filename;
            loaded = IMG_Load(fallback.c_str());
            if (!loaded) {
                std::cerr << "Failed to load fallback image: " << fallback << " error: " << SDL_GetError() << std::endl;
                return nullptr;
            }
        }

        SDL_Surface* rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(loaded);
        if (!rgba) return nullptr;

        DDSURFACEDESC desc = {};
        desc.dwSize = sizeof(DDSURFACEDESC);
        desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
        desc.dwWidth = (DWORD)rgba->w;
        desc.dwHeight = (DWORD)rgba->h;
        desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
        desc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
        desc.ddpfPixelFormat.dwFlags = DDPF_RGB;
        desc.ddpfPixelFormat.dwRGBBitCount = 32;

        LPDIRECTDRAWSURFACE dds = nullptr;
        if (FAILED(dd->CreateSurface(&desc, &dds, nullptr))) {
            SDL_DestroySurface(rgba);
            return nullptr;
        }

        DDSURFACEDESC lock = {};
        lock.dwSize = sizeof(DDSURFACEDESC);
        if (SUCCEEDED(dds->Lock(NULL, &lock, 0, NULL))) {
            BYTE* dst = (BYTE*)lock.lpSurface;
            BYTE* src = (BYTE*)rgba->pixels;
            for (int y = 0; y < rgba->h; ++y) {
                memcpy(dst + y * lock.lPitch, src + y * rgba->pitch, rgba->w * 4);
            }
            dds->Unlock(NULL);
        }

        SDL_DestroySurface(rgba);
        return dds;
    };

    LPDIRECTDRAWSURFACE playerSurface = loadSurfaceFromFile(directDraw, "player.png");
    if (playerSurface) {
        DDCOLORKEY ck = {0, 0}; // Assuming 0 is transparent if needed, or set from alpha
        playerSurface->SetColorKey(DDCKEY_SRCBLT, &ck);
    }

    // 8-bit Paletted Sprite Surface
    surfaceDesc = {};
    surfaceDesc.dwSize = sizeof(DDSURFACEDESC);
    surfaceDesc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    surfaceDesc.dwWidth = 64;
    surfaceDesc.dwHeight = 64;
    surfaceDesc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    surfaceDesc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    surfaceDesc.ddpfPixelFormat.dwFlags = DDPF_PALETTEINDEXED8;
    surfaceDesc.ddpfPixelFormat.dwRGBBitCount = 8;

    LPDIRECTDRAWSURFACE spriteSurface = nullptr;
    if (FAILED(directDraw->CreateSurface(&surfaceDesc, &spriteSurface, nullptr))) {
        backSurface->Release();
        primarySurface->Release();
        directDraw->Release();
        DestroyWindow(window);
        return 1;
    }

    // Palette
    PALETTEENTRY palette[256] = {};
    palette[0] = {255, 0, 255, 0}; // Color key (magenta)
    for (int i = 1; i < 256; ++i) {
        palette[i].peRed = (BYTE)(i);
        palette[i].peGreen = (BYTE)(255 - i);
        palette[i].peBlue = (BYTE)((i * 2) % 256);
    }

    LPDIRECTDRAWPALETTE ddPalette = nullptr;
    if (SUCCEEDED(directDraw->CreatePalette(DDPCAPS_8BIT, palette, &ddPalette, nullptr))) {
        spriteSurface->SetPalette(ddPalette);
        ddPalette->Release();
    }

    // Fill sprite with Lock/Unlock
    DDSURFACEDESC lockDesc = {};
    lockDesc.dwSize = sizeof(DDSURFACEDESC);
    if (SUCCEEDED(spriteSurface->Lock(NULL, &lockDesc, 0, NULL))) {
        BYTE* pixels = (BYTE*)lockDesc.lpSurface;
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                float dx = (float)x - 32.0f;
                float dy = (float)y - 32.0f;
                float dist = std::sqrt(dx*dx + dy*dy);
                if (dist < 30.0f) {
                    pixels[y * lockDesc.lPitch + x] = (BYTE)(dist * 8 + 10);
                } else {
                    pixels[y * lockDesc.lPitch + x] = 0; // Transparent
                }
            }
        }
        spriteSurface->Unlock(NULL);
    }

    // Set Color Key
    DDCOLORKEY colorKey = {0, 0};
    spriteSurface->SetColorKey(DDCKEY_SRCBLT, &colorKey);

    // Clipper (basic support)
    LPDIRECTDRAWCLIPPER clipper = nullptr;
    if (SUCCEEDED(directDraw->CreateClipper(0, &clipper, nullptr))) {
        clipper->SetHWnd(0, window);
        backSurface->SetClipper(clipper);
        clipper->Release();
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

        if (!running) break;

        // 1. Clear back buffer
        DDBLTFX clearFx{};
        clearFx.dwSize = sizeof(DDBLTFX);
        clearFx.dwFillColor = 0x00202020;
        backSurface->Blt(NULL, NULL, NULL, DDBLT_COLORFILL, &clearFx);

        // 2. Draw background pattern via Lock/Unlock
        DDSURFACEDESC backLock = {};
        backLock.dwSize = sizeof(DDSURFACEDESC);
        if (SUCCEEDED(backSurface->Lock(NULL, &backLock, 0, NULL))) {
            DWORD* pixels = (DWORD*)backLock.lpSurface;
            for (int y = 0; y < 600; y += 20) {
                for (int x = 0; x < 800; x += 20) {
                    int offset = (x + frame) % 40;
                    if (offset < 20) {
                         // draw a dot
                         pixels[y * (backLock.lPitch / 4) + x] = 0x00444444;
                    }
                }
            }
            backSurface->Unlock(NULL);
        }

        // 3. Draw moving rectangles (Blt color fill)
        for (int i = 0; i < 5; ++i) {
            float phase = (float)frame * 0.05f + (float)i;
            RECT r = { 
                100 + i * 120, 
                200 + (int)(100 * std::sin(phase)), 
                200 + i * 120, 
                300 + (int)(100 * std::sin(phase)) 
            };
            DDBLTFX rfx{};
            rfx.dwSize = sizeof(DDBLTFX);
            rfx.dwFillColor = (0x00FF0000 >> (i * 4)) | (0x0000FF00 << (i * 2));
            backSurface->Blt(&r, NULL, NULL, DDBLT_COLORFILL, &rfx);
        }

        // 4. Draw sprites (BltFast with color key)
        for (int i = 0; i < 10; ++i) {
            int sx = 400 + (int)(300 * std::cos((float)frame * 0.02f + (float)i * 0.6f)) - 32;
            int sy = 300 + (int)(250 * std::sin((float)frame * 0.03f + (float)i * 0.6f)) - 32;
            backSurface->BltFast((DWORD)sx, (DWORD)sy, spriteSurface, NULL, DDBLTFAST_SRCCOLORKEY);
        }

    // 5. Draw scaled sprite (Blt)
        RECT dstRect = { 10, 10, 138, 138 };
        backSurface->Blt(&dstRect, spriteSurface, NULL, DDBLT_WAIT, NULL);

        // 5b. Software "Transformation" Demo (Shear)
        if (SUCCEEDED(spriteSurface->Lock(NULL, &lockDesc, 0, NULL))) {
            BYTE* pixels = (BYTE*)lockDesc.lpSurface;
            float shear = std::sin((float)frame * 0.1f) * 0.5f;
            for (int y = 0; y < 64; ++y) {
                for (int x = 0; x < 64; ++x) {
                    float dx = (float)x - 32.0f;
                    float dy = (float)y - 32.0f;
                    // Apply shear
                    float nx = dx + dy * shear;
                    float ny = dy;
                    float dist = std::sqrt(nx*nx + ny*ny);
                    if (dist < 25.0f) {
                        pixels[y * lockDesc.lPitch + x] = (BYTE)(dist * 8 + 50 + (int)(std::sin((float)frame*0.1f)*20));
                    } else {
                        pixels[y * lockDesc.lPitch + x] = 0;
                    }
                }
            }
            spriteSurface->Unlock(NULL);
        }

        // 6. Start Primary Rendering
        DDBLTFX clearFxP{};
        clearFxP.dwSize = sizeof(DDBLTFX);
        clearFxP.dwFillColor = 0x00000000;
        primarySurface->Blt(NULL, NULL, NULL, DDBLT_COLORFILL, &clearFxP);

        // 7. Blit backbuffer to primary
        primarySurface->Blt(NULL, backSurface, NULL, DDBLT_WAIT, NULL);

        // 8. Draw rotating player texture (if loaded) directly on primary
        if (playerSurface) {
            for (int i = 0; i < 3; ++i) {
                float angle = (float)frame * (1.5f + i * 0.5f);
                float scale = 0.5f + 0.5f * std::sin((float)frame * 0.02f + i);
                int size = (int)(128 * scale);
                int px = 200 + i * 200 + (int)(50 * std::cos((float)frame * 0.03f));
                int py = 150 + (int)(50 * std::sin((float)frame * 0.03f));
                
                RECT pDst = { px - size/2, py - size/2, px + size/2, py + size/2 };
                DDBLTFX rotFx = {};
                rotFx.dwSize = sizeof(DDBLTFX);
                rotFx.dwRotationAngle = (DWORD)angle;
                
                primarySurface->Blt(&pDst, playerSurface, NULL, DDBLT_WAIT | DDBLT_ROTATIONANGLE, &rotFx);
            }
        }

        // 9. Flip
        primarySurface->Flip(NULL, 0);

        frame++;
        Sleep(16);
    }

    if (playerSurface) playerSurface->Release();
    spriteSurface->Release();
    backSurface->Release();
    primarySurface->Release();
    directDraw->Release();
    DestroyWindow(window);
    return 0;
}
