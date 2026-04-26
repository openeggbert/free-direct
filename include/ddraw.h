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
#define DDERR_WASSTILLDRAWING ((HRESULT)0x8876021CL)

// Legacy DirectDraw error constants used by the original source - Status: STUB
#define DDERR_ALREADYINITIALIZED ((HRESULT)0x88761001L)
#define DDERR_CANNOTATTACHSURFACE ((HRESULT)0x88761002L)
#define DDERR_CANNOTDETACHSURFACE ((HRESULT)0x88761003L)
#define DDERR_CURRENTLYNOTAVAIL ((HRESULT)0x88761004L)
#define DDERR_EXCEPTION ((HRESULT)0x88761005L)
#define DDERR_HEIGHTALIGN ((HRESULT)0x88761006L)
#define DDERR_INCOMPATIBLEPRIMARY ((HRESULT)0x88761007L)
#define DDERR_INVALIDCAPS ((HRESULT)0x88761008L)
#define DDERR_INVALIDCLIPLIST ((HRESULT)0x88761009L)
#define DDERR_INVALIDMODE ((HRESULT)0x8876100AL)
#define DDERR_INVALIDOBJECT ((HRESULT)0x8876100BL)
#define DDERR_INVALIDPIXELFORMAT ((HRESULT)0x8876100CL)
#define DDERR_INVALIDRECT ((HRESULT)0x8876100DL)
#define DDERR_LOCKEDSURFACES ((HRESULT)0x8876100EL)
#define DDERR_NO3D ((HRESULT)0x8876100FL)
#define DDERR_NOALPHAHW ((HRESULT)0x88761010L)
#define DDERR_NOCLIPLIST ((HRESULT)0x88761011L)
#define DDERR_NOCOLORCONVHW ((HRESULT)0x88761012L)
#define DDERR_NOCOOPERATIVELEVELSET ((HRESULT)0x88761013L)
#define DDERR_NOCOLORKEY ((HRESULT)0x88761014L)
#define DDERR_NOCOLORKEYHW ((HRESULT)0x88761015L)
#define DDERR_NODIRECTDRAWSUPPORT ((HRESULT)0x88761016L)
#define DDERR_NOEXCLUSIVEMODE ((HRESULT)0x88761017L)
#define DDERR_NOFLIPHW ((HRESULT)0x88761018L)
#define DDERR_NOGDI ((HRESULT)0x88761019L)
#define DDERR_NOMIRRORHW ((HRESULT)0x8876101AL)
#define DDERR_NOTFOUND ((HRESULT)0x8876101BL)
#define DDERR_NOOVERLAYHW ((HRESULT)0x8876101CL)
#define DDERR_NORASTEROPHW ((HRESULT)0x8876101DL)
#define DDERR_NOROTATIONHW ((HRESULT)0x8876101EL)
#define DDERR_NOSTRETCHHW ((HRESULT)0x8876101FL)
#define DDERR_NOT4BITCOLOR ((HRESULT)0x88761020L)
#define DDERR_NOT4BITCOLORINDEX ((HRESULT)0x88761021L)
#define DDERR_NOT8BITCOLOR ((HRESULT)0x88761022L)
#define DDERR_NOTEXTUREHW ((HRESULT)0x88761023L)
#define DDERR_NOVSYNCHW ((HRESULT)0x88761024L)
#define DDERR_NOZBUFFERHW ((HRESULT)0x88761025L)
#define DDERR_NOZOVERLAYHW ((HRESULT)0x88761026L)
#define DDERR_OUTOFCAPS ((HRESULT)0x88761027L)
#define DDERR_OUTOFVIDEOMEMORY ((HRESULT)0x88761028L)
#define DDERR_OVERLAYCANTCLIP ((HRESULT)0x88761029L)
#define DDERR_OVERLAYCOLORKEYONLYONEACTIVE ((HRESULT)0x8876102AL)
#define DDERR_PALETTEBUSY ((HRESULT)0x8876102BL)
#define DDERR_COLORKEYNOTSET ((HRESULT)0x8876102CL)
#define DDERR_SURFACEALREADYATTACHED ((HRESULT)0x8876102DL)
#define DDERR_SURFACEALREADYDEPENDENT ((HRESULT)0x8876102EL)
#define DDERR_SURFACEBUSY ((HRESULT)0x8876102FL)
#define DDERR_CANTLOCKSURFACE ((HRESULT)0x88761030L)
#define DDERR_SURFACEISOBSCURED ((HRESULT)0x88761031L)
#define DDERR_SURFACELOST ((HRESULT)0x88761032L)
#define DDERR_SURFACENOTATTACHED ((HRESULT)0x88761033L)
#define DDERR_TOOBIGHEIGHT ((HRESULT)0x88761034L)
#define DDERR_TOOBIGSIZE ((HRESULT)0x88761035L)
#define DDERR_TOOBIGWIDTH ((HRESULT)0x88761036L)
#define DDERR_UNSUPPORTEDFORMAT ((HRESULT)0x88761037L)
#define DDERR_UNSUPPORTEDMASK ((HRESULT)0x88761038L)
#define DDERR_VERTICALBLANKINPROGRESS ((HRESULT)0x88761039L)
#define DDERR_XALIGN ((HRESULT)0x8876103AL)
#define DDERR_INVALIDDIRECTDRAWGUID ((HRESULT)0x8876103BL)
#define DDERR_DIRECTDRAWALREADYCREATED ((HRESULT)0x8876103CL)
#define DDERR_NODIRECTDRAWHW ((HRESULT)0x8876103DL)
#define DDERR_PRIMARYSURFACEALREADYEXISTS ((HRESULT)0x8876103EL)
#define DDERR_NOEMULATION ((HRESULT)0x8876103FL)
#define DDERR_REGIONTOOSMALL ((HRESULT)0x88761040L)
#define DDERR_CLIPPERISUSINGHWND ((HRESULT)0x88761041L)
#define DDERR_NOCLIPPERATTACHED ((HRESULT)0x88761042L)
#define DDERR_NOHWND ((HRESULT)0x88761043L)
#define DDERR_HWNDSUBCLASSED ((HRESULT)0x88761044L)
#define DDERR_HWNDALREADYSET ((HRESULT)0x88761045L)
#define DDERR_NOPALETTEATTACHED ((HRESULT)0x88761046L)
#define DDERR_NOPALETTEHW ((HRESULT)0x88761047L)
#define DDERR_BLTFASTCANTCLIP ((HRESULT)0x88761048L)
#define DDERR_NOBLTHW ((HRESULT)0x88761049L)
#define DDERR_NODDROPSHW ((HRESULT)0x8876104AL)
#define DDERR_OVERLAYNOTVISIBLE ((HRESULT)0x8876104BL)
#define DDERR_NOOVERLAYDEST ((HRESULT)0x8876104CL)
#define DDERR_INVALIDPOSITION ((HRESULT)0x8876104DL)
#define DDERR_NOTAOVERLAYSURFACE ((HRESULT)0x8876104EL)
#define DDERR_EXCLUSIVEMODEALREADYSET ((HRESULT)0x8876104FL)
#define DDERR_NOTFLIPPABLE ((HRESULT)0x88761050L)
#define DDERR_CANTDUPLICATE ((HRESULT)0x88761051L)
#define DDERR_NOTLOCKED ((HRESULT)0x88761052L)
#define DDERR_CANTCREATEDC ((HRESULT)0x88761053L)
#define DDERR_NODC ((HRESULT)0x88761054L)
#define DDERR_WRONGMODE ((HRESULT)0x88761055L)
#define DDERR_IMPLICITLYCREATED ((HRESULT)0x88761056L)
#define DDERR_NOTPALETTIZED ((HRESULT)0x88761057L)
#define DDERR_UNSUPPORTEDMODE ((HRESULT)0x88761058L)
#define DDERR_NOMIPMAPHW ((HRESULT)0x88761059L)
#define DDERR_INVALIDSURFACETYPE ((HRESULT)0x8876105AL)
#define DDERR_DCALREADYCREATED ((HRESULT)0x8876105BL)
#define DDERR_CANTPAGELOCK ((HRESULT)0x8876105CL)
#define DDERR_CANTPAGEUNLOCK ((HRESULT)0x8876105DL)
#define DDERR_NOTPAGELOCKED ((HRESULT)0x8876105EL)
#define DDERR_NOTINITIALIZED ((HRESULT)0x8876105FL)

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

#ifndef FREE_API_PALETTEENTRY_DEFINED
#define FREE_API_PALETTEENTRY_DEFINED
typedef struct tagPALETTEENTRY {
    BYTE peRed;
    BYTE peGreen;
    BYTE peBlue;
    BYTE peFlags;
} PALETTEENTRY, *PPALETTEENTRY, *LPPALETTEENTRY;
#else
typedef PALETTEENTRY* PPALETTEENTRY;
#endif

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