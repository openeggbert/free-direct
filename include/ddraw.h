/**
 * @file ddraw.h
 * @brief Narrow DirectX 3 / DirectDraw subset reimplementation.
 * @note Status: PARTIAL
 */
#ifndef FREE_DIRECT_DDRAW_H
#define FREE_DIRECT_DDRAW_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef LONG HRESULT;
typedef struct IUnknown IUnknown;

#ifndef SUCCEEDED
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#endif

#ifndef FAILED
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#endif

#define DD_OK ((HRESULT)0L)
#define DDERR_GENERIC ((HRESULT)0x88760000L)
#define DDERR_INVALIDPARAMS ((HRESULT)0x88760064L)
#define DDERR_OUTOFMEMORY ((HRESULT)0x8876017CL)
#define DDERR_UNSUPPORTED ((HRESULT)0x88760032L)

#define DDSCAPS_PRIMARYSURFACE 0x00000200L
#define DDSCAPS_OFFSCREENPLAIN 0x00000040L

#define DDSD_CAPS 0x00000001L
#define DDSD_HEIGHT 0x00000002L
#define DDSD_WIDTH 0x00000004L

#define DDBLT_WAIT 0x00000010L
#define DDBLT_COLORFILL 0x00000400L

typedef struct _DDSCAPS {
    DWORD dwCaps;
} DDSCAPS, *LPDDSCAPS;

typedef struct _DDSURFACEDESC {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwWidth;
    DWORD dwHeight;
    DDSCAPS ddsCaps;
} DDSURFACEDESC, *LPDDSURFACEDESC;

typedef struct _DDBLTFX {
    DWORD dwSize;
    DWORD dwFillColor;
} DDBLTFX, *LPDDBLTFX;

struct IDirectDraw;
struct IDirectDrawSurface;

typedef struct IDirectDraw* LPDIRECTDRAW;
typedef struct IDirectDrawSurface* LPDIRECTDRAWSURFACE;

HRESULT WINAPI DirectDrawCreate(const GUID* lpGUID, LPDIRECTDRAW* lplpDD, IUnknown* pUnkOuter);

#ifdef __cplusplus
}

class IDirectDraw {
public:
    /** @note Status: STUB */
    virtual HRESULT WINAPI QueryInterface(const GUID& riid, void** ppvObject) = 0;
    /** @note Status: IMPLEMENTED */
    virtual ULONG WINAPI AddRef() = 0;
    /** @note Status: IMPLEMENTED */
    virtual ULONG WINAPI Release() = 0;
    /** @note Status: IMPLEMENTED (Minimal SDL3 mapping) */
    virtual HRESULT WINAPI SetCooperativeLevel(HWND hWnd, DWORD dwFlags) = 0;
    /** @note Status: PARTIAL (Supports Primary and Offscreen) */
    virtual HRESULT WINAPI CreateSurface(const DDSURFACEDESC* lpDDSurfaceDesc,
                                         LPDIRECTDRAWSURFACE* lplpDDSurface,
                                         IUnknown* pUnkOuter) = 0;

protected:
    virtual ~IDirectDraw() = default;
};

class IDirectDrawSurface {
public:
    /** @note Status: STUB */
    virtual HRESULT WINAPI QueryInterface(const GUID& riid, void** ppvObject) = 0;
    /** @note Status: IMPLEMENTED */
    virtual ULONG WINAPI AddRef() = 0;
    /** @note Status: IMPLEMENTED */
    virtual ULONG WINAPI Release() = 0;
    /** @note Status: PARTIAL (ColorFill and Surface-to-Surface Blit) */
    virtual HRESULT WINAPI Blt(LPRECT lpDestRect,
                               LPDIRECTDRAWSURFACE lpDDSrcSurface,
                               LPRECT lpSrcRect,
                               DWORD dwFlags,
                               LPDDBLTFX lpDDBltFx) = 0;
    /** @note Status: IMPLEMENTED (Simplified Present) */
    virtual HRESULT WINAPI Flip(LPDIRECTDRAWSURFACE lpDDSurfaceTargetOverride, DWORD dwFlags) = 0;

protected:
    virtual ~IDirectDrawSurface() = default;
};

#endif

#endif