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
#define DDSCAPS_SYSTEMMEMORY   0x00000800L

#define DDSD_CAPS   0x00000001L
#define DDSD_HEIGHT 0x00000002L
#define DDSD_WIDTH  0x00000004L
#define DDSD_PITCH  0x00000008L
#define DDSD_LPSURFACE 0x00000800L
#define DDSD_PIXELFORMAT 0x00001000L

#define DDBLT_WAIT      0x00000010L
#define DDBLT_COLORFILL 0x00000400L
#define DDBLT_ROTATIONANGLE 0x01000000L

#define DDBLTFAST_SRCCOLORKEY 0x00000001L
#define DDBLTFAST_NOCOLORKEY  0x00000000L

#define DDCKEY_SRCBLT 0x00000008L

#define DDSCL_FULLSCREEN 0x00000001L
#define DDSCL_EXCLUSIVE  0x00000010L
#define DDSCL_NORMAL     0x00000008L

#define DDPCAPS_8BIT 0x00000004L

#define DDPF_RGB              0x00000040L
#define DDPF_PALETTEINDEXED8  0x00000020L

typedef struct _DDCOLORKEY {
    DWORD dwColorSpaceLowValue;
    DWORD dwColorSpaceHighValue;
} DDCOLORKEY, *LPDDCOLORKEY;

typedef struct _DDSCAPS {
    DWORD dwCaps;
} DDSCAPS, *LPDDSCAPS;

typedef struct _DDPIXELFORMAT {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwFourCC;
    union {
        DWORD dwRGBBitCount;
        DWORD dwYUVBitCount;
        DWORD dwZBufferBitDepth;
        DWORD dwAlphaBitDepth;
    };
    union {
        DWORD dwRBitMask;
        DWORD dwYBitMask;
    };
    union {
        DWORD dwGBitMask;
        DWORD dwUBitMask;
    };
    union {
        DWORD dwBBitMask;
        DWORD dwVBitMask;
    };
    union {
        DWORD dwRGBAlphaBitMask;
        DWORD dwYUVAlphaBitMask;
    };
} DDPIXELFORMAT, *LPDDPIXELFORMAT;

typedef struct _DDSURFACEDESC {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwHeight;
    DWORD dwWidth;
    union {
        LONG lPitch;
        DWORD dwLinearSize;
    };
    DWORD dwBackBufferCount;
    union {
        DWORD dwMipMapCount;
        DWORD dwZBufferBitDepth;
        DWORD dwRefreshRate;
    };
    DWORD dwAlphaBitDepth;
    DWORD dwReserved;
    LPVOID lpSurface;
    DDCOLORKEY ddckCKDestOverlay;
    DDCOLORKEY ddckCKDestBlt;
    DDCOLORKEY ddckCKSrcOverlay;
    DDCOLORKEY ddckCKSrcBlt;
    DDPIXELFORMAT ddpfPixelFormat;
    DDSCAPS ddsCaps;
} DDSURFACEDESC, *LPDDSURFACEDESC;

typedef struct _DDBLTFX {
    DWORD dwSize;
    DWORD dwDDFX;
    DWORD dwROP;
    DWORD dwDDROP;
    DWORD dwRotationAngle;
    DWORD dwZBufferDestConst;
    DWORD dwZBufferDestOverlap;
    DWORD dwZBufferSrcConst;
    DWORD dwZBufferSrcOverlap;
    DWORD dwYCbCrDestConst;
    DWORD dwYCbCrSrcConst;
    DWORD dwAlphaDestConst;
    DWORD dwAlphaSrcConst;
    DWORD dwFillColor;
    DDCOLORKEY ddckDestColorkey;
    DDCOLORKEY ddckSrcColorkey;
} DDBLTFX, *LPDDBLTFX;

typedef struct tagPALETTEENTRY {
    BYTE peRed;
    BYTE peGreen;
    BYTE peBlue;
    BYTE peFlags;
} PALETTEENTRY, *PPALETTEENTRY, *LPPALETTEENTRY;

struct IDirectDraw;
struct IDirectDrawSurface;
struct IDirectDrawPalette;
struct IDirectDrawClipper;

typedef struct IDirectDraw* LPDIRECTDRAW;
typedef struct IDirectDrawSurface* LPDIRECTDRAWSURFACE;
typedef struct IDirectDrawPalette* LPDIRECTDRAWPALETTE;
typedef struct IDirectDrawClipper* LPDIRECTDRAWCLIPPER;

HRESULT WINAPI DirectDrawCreate(const GUID* lpGUID, LPDIRECTDRAW* lplpDD, IUnknown* pUnkOuter);

#ifdef __cplusplus
}

class IDirectDrawPalette {
public:
    virtual HRESULT WINAPI QueryInterface(const GUID& riid, void** ppvObject) = 0;
    virtual ULONG WINAPI AddRef() = 0;
    virtual ULONG WINAPI Release() = 0;
    virtual HRESULT WINAPI GetEntries(DWORD dwFlags, DWORD dwBase, DWORD dwNumEntries, LPPALETTEENTRY lpEntries) = 0;
    virtual HRESULT WINAPI SetEntries(DWORD dwFlags, DWORD dwBase, DWORD dwNumEntries, LPPALETTEENTRY lpEntries) = 0;

protected:
    virtual ~IDirectDrawPalette() = default;
};

class IDirectDrawClipper {
public:
    virtual HRESULT WINAPI QueryInterface(const GUID& riid, void** ppvObject) = 0;
    virtual ULONG WINAPI AddRef() = 0;
    virtual ULONG WINAPI Release() = 0;
    virtual HRESULT WINAPI SetHWnd(DWORD dwFlags, HWND hWnd) = 0;

protected:
    virtual ~IDirectDrawClipper() = default;
};

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
    /** @note Status: IMPLEMENTED */
    virtual HRESULT WINAPI SetDisplayMode(DWORD dwWidth, DWORD dwHeight, DWORD dwBPP) = 0;
    /** @note Status: IMPLEMENTED */
    virtual HRESULT WINAPI CreatePalette(DWORD dwFlags, LPPALETTEENTRY lpColorTable, LPDIRECTDRAWPALETTE* lplpDDPalette, IUnknown* pUnkOuter) = 0;
    /** @note Status: IMPLEMENTED */
    virtual HRESULT WINAPI CreateClipper(DWORD dwFlags, LPDIRECTDRAWCLIPPER* lplpDDClipper, IUnknown* pUnkOuter) = 0;

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
    /** @note Status: IMPLEMENTED */
    virtual HRESULT WINAPI BltFast(DWORD dwX, DWORD dwY, LPDIRECTDRAWSURFACE lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans) = 0;
    /** @note Status: IMPLEMENTED (Simplified Present) */
    virtual HRESULT WINAPI Flip(LPDIRECTDRAWSURFACE lpDDSurfaceTargetOverride, DWORD dwFlags) = 0;
    /** @note Status: IMPLEMENTED */
    virtual HRESULT WINAPI SetClipper(LPDIRECTDRAWCLIPPER lpDDClipper) = 0;
    /** @note Status: IMPLEMENTED */
    virtual HRESULT WINAPI SetPalette(LPDIRECTDRAWPALETTE lpDDPalette) = 0;
    /** @note Status: IMPLEMENTED */
    virtual HRESULT WINAPI IsLost() = 0;
    /** @note Status: IMPLEMENTED */
    virtual HRESULT WINAPI Restore() = 0;
    /** @note Status: STUB */
    virtual HRESULT WINAPI GetDC(HDC* lphDC) = 0;
    /** @note Status: STUB */
    virtual HRESULT WINAPI ReleaseDC(HDC hDC) = 0;
    /** @note Status: IMPLEMENTED */
    virtual HRESULT WINAPI GetSurfaceDesc(LPDDSURFACEDESC lpDDSurfaceDesc) = 0;
    /** @note Status: IMPLEMENTED */
    virtual HRESULT WINAPI Lock(LPRECT lpDestRect, LPDDSURFACEDESC lpDDSurfaceDesc, DWORD dwFlags, HANDLE hEvent) = 0;
    /** @note Status: IMPLEMENTED */
    virtual HRESULT WINAPI Unlock(LPVOID lpSurfaceData) = 0;
    /** @note Status: IMPLEMENTED */
    virtual HRESULT WINAPI SetColorKey(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey) = 0;

protected:
    virtual ~IDirectDrawSurface() = default;
};

#endif

#endif