#include "CKRasterizer.h"

CKDWORD GetMsb(CKDWORD num, CKDWORD max)
{
#define OPERAND_SIZE (sizeof(CKDWORD) * 8)
    CKDWORD i = OPERAND_SIZE - 1;
#ifdef WIN32
    __asm
    {
        mov eax, num
        bsr eax, eax
        mov i, eax
    }
#else
    if (num != 0)
        while (!(num & (1 << (OPERAND_SIZE - 1))))
        {
            num <<= 1;
            --i;
        }
#endif
    return (i > max) ? max : i;
#undef OPERAND_SIZE
}

CKDWORD GetLsb(CKDWORD num, CKDWORD max)
{
    CKDWORD i = 0;
#ifdef WIN32
    __asm
    {
        mov eax, num
        bsf eax, eax
        mov i, eax
    }
#else
    if (num != 0)
        while (!(num & 1))
        {
            num >>= 1;
            ++i;
        }
#endif
    return (i > max) ? max : i;
}

inline CKDWORD GetPow2(CKDWORD num)
{
    // Special case for 0
    if (num == 0) return 1;

    // Check if already a power of 2
    if ((num & (num - 1)) == 0)
        return num;

    // Find MSB and return next power of 2
    CKDWORD msb = GetMsb(num, sizeof(CKDWORD) * 8);
    return 1 << (msb + 1);
}

CKRasterizerContext::CKRasterizerContext()
    : m_Textures(),
      m_Sprites(),
      m_VertexBuffers(),
      m_IndexBuffers(),
      m_VertexShaders(),
      m_PixelFormat(),
      m_CurrentMaterialData(),
      m_CurrentLightData(),
      m_DirtyRects()
{
    m_Driver = NULL;
    m_PosX = 0;
    m_PosY = 0;
    m_Width = 0;
    m_Height = 0;
    m_Window = NULL;
    m_Fullscreen = 0;
    m_RefreshRate = 0;
    m_SceneBegined = FALSE;
    m_MatrixUptodate = 0;
    m_TransparentMode = 0;
    m_Bpp = 0;
    m_ZBpp = 0;
    m_PixelFormat = UNKNOWN_PF;
    m_StencilBpp = 0;

    m_TotalMatrix = VxMatrix::Identity();
    m_WorldMatrix = VxMatrix::Identity();
    m_ViewMatrix = VxMatrix::Identity();
    m_ProjectionMatrix = VxMatrix::Identity();

    m_Textures.Resize(INIT_OBJECTSLOTS);
    m_Sprites.Resize(INIT_OBJECTSLOTS);
    m_VertexBuffers.Resize(INIT_OBJECTSLOTS);
    m_IndexBuffers.Resize(INIT_OBJECTSLOTS);
    m_VertexShaders.Resize(INIT_OBJECTSLOTS);
    m_PixelShaders.Resize(INIT_OBJECTSLOTS);

    m_Textures.Memset(NULL);
    m_Sprites.Memset(NULL);
    m_VertexBuffers.Memset(NULL);
    m_IndexBuffers.Memset(NULL);
    m_VertexShaders.Memset(NULL);
    m_PixelShaders.Memset(NULL);

    m_PresentInterval = 0;
    m_CurrentPresentInterval = 0;
    m_Antialias = 0;
    m_EnableScreenDump = 0;

    memset(m_StateCache, 0, sizeof(m_StateCache));
    InitDefaultRenderStatesValue();
    FlushRenderStateCache();
    m_RenderStateCacheMiss = 0;
    m_RenderStateCacheHit = 0;

    m_InverseWinding = 0;
    m_EnsureVertexShader = 0;
    m_UnityMatrixMask = 0;
}

CKRasterizerContext::~CKRasterizerContext() {}

CKBOOL CKRasterizerContext::SetMaterial(CKMaterialData *mat)
{
    if (mat)
        memcpy(&m_CurrentMaterialData, mat, sizeof(m_CurrentMaterialData));
    return FALSE;
}

CKBOOL CKRasterizerContext::SetViewport(CKViewportData *data)
{
    memcpy(&m_ViewportData, data, sizeof(m_ViewportData));
    return TRUE;
}

CKBOOL CKRasterizerContext::SetTransformMatrix(VXMATRIX_TYPE Type, const VxMatrix &Mat)
{
    switch (Type)
    {
    case VXMATRIX_WORLD:
        memcpy(&m_WorldMatrix, Mat, sizeof(m_WorldMatrix));
        Vx3DMultiplyMatrix(m_ModelViewMatrix, m_ViewMatrix, m_WorldMatrix);
        m_MatrixUptodate &= 0xFE;
        break;
    case VXMATRIX_VIEW:
        memcpy(&m_ViewMatrix, Mat, sizeof(m_ViewMatrix));
        Vx3DMultiplyMatrix(m_ModelViewMatrix, m_ViewMatrix, m_WorldMatrix);
        m_MatrixUptodate = 0;
        break;
    case VXMATRIX_PROJECTION:
        memcpy(&m_ProjectionMatrix, Mat, sizeof(m_ProjectionMatrix));
        m_MatrixUptodate = 0;
        break;
    default:
        break;
    }
    return TRUE;
}

CKBOOL CKRasterizerContext::DeleteObject(CKDWORD ObjIndex, CKRST_OBJECTTYPE Type)
{
    switch (Type)
    {
    case CKRST_OBJ_TEXTURE:
        if (ObjIndex < m_Textures.Size())
        {
            delete m_Textures[ObjIndex];
            m_Textures[ObjIndex] = NULL;
        }
        break;
    case CKRST_OBJ_SPRITE:
        if (ObjIndex < m_Sprites.Size())
        {
            delete m_Sprites[ObjIndex];
            m_Sprites[ObjIndex] = NULL;
        }
        break;
    case CKRST_OBJ_VERTEXBUFFER:
        if (ObjIndex < m_VertexBuffers.Size())
        {
            delete m_VertexBuffers[ObjIndex];
            m_VertexBuffers[ObjIndex] = NULL;
        }
        break;
    case CKRST_OBJ_INDEXBUFFER:
        if (ObjIndex < m_IndexBuffers.Size())
        {
            delete m_IndexBuffers[ObjIndex];
            m_IndexBuffers[ObjIndex] = NULL;
        }
        break;
    case CKRST_OBJ_VERTEXSHADER:
        if (ObjIndex < m_VertexShaders.Size())
        {
            delete m_VertexShaders[ObjIndex];
            m_VertexShaders[ObjIndex] = NULL;
        }
        break;
    case CKRST_OBJ_PIXELSHADER:
        if (ObjIndex < m_PixelShaders.Size())
        {
            delete m_PixelShaders[ObjIndex];
            m_PixelShaders[ObjIndex] = NULL;
        }
        break;
    default:
        return FALSE;
    }
    return TRUE;
}

CKBOOL CKRasterizerContext::FlushObjects(CKDWORD TypeMask)
{
    if ((TypeMask & CKRST_OBJ_TEXTURE) != 0)
        for (XArray<CKTextureDesc *>::Iterator it = m_Textures.Begin(); it != m_Textures.End(); ++it)
        {
            if (*it)
                delete *it;
            *it = NULL;
        }

    if ((TypeMask & CKRST_OBJ_SPRITE) != 0)
        for (XArray<CKSpriteDesc *>::Iterator it = m_Sprites.Begin(); it != m_Sprites.End(); ++it)
        {
            if (*it)
                delete *it;
            *it = NULL;
        }

    if ((TypeMask & CKRST_OBJ_VERTEXBUFFER) != 0)
        for (XArray<CKVertexBufferDesc *>::Iterator it = m_VertexBuffers.Begin(); it != m_VertexBuffers.End(); ++it)
        {
            if (*it)
                delete *it;
            *it = NULL;
        }

    if ((TypeMask & CKRST_OBJ_INDEXBUFFER) != 0)
        for (XArray<CKIndexBufferDesc *>::Iterator it = m_IndexBuffers.Begin(); it != m_IndexBuffers.End(); ++it)
        {
            if (*it)
                delete *it;
            *it = NULL;
        }

    if ((TypeMask & CKRST_OBJ_VERTEXSHADER) != 0)
        for (XArray<CKVertexShaderDesc *>::Iterator it = m_VertexShaders.Begin(); it != m_VertexShaders.End(); ++it)
        {
            if (*it)
                delete *it;
            *it = NULL;
        }

    if ((TypeMask & CKRST_OBJ_PIXELSHADER) != 0)
        for (XArray<CKPixelShaderDesc *>::Iterator it = m_PixelShaders.Begin(); it != m_PixelShaders.End(); ++it)
        {
            if (*it)
                delete *it;
            *it = NULL;
        }

    return TRUE;
}

void CKRasterizerContext::UpdateObjectArrays(CKRasterizer *rst)
{
    int newSize = rst->m_ObjectsIndex.Size();
    int oldSize = m_Textures.Size();
    if (newSize != oldSize)
    {
        m_Textures.Resize(newSize);
        m_Sprites.Resize(newSize);
        m_VertexBuffers.Resize(newSize);
        m_IndexBuffers.Resize(newSize);
        m_VertexShaders.Resize(newSize);
        m_PixelShaders.Resize(newSize);

        int gapSize = (newSize - oldSize) * sizeof(void *);
        memset(&m_Textures[oldSize], 0, gapSize);
        memset(&m_Sprites[oldSize], 0, gapSize);
        memset(&m_VertexBuffers[oldSize], 0, gapSize);
        memset(&m_IndexBuffers[oldSize], 0, gapSize);
        memset(&m_VertexShaders[oldSize], 0, gapSize);
        memset(&m_PixelShaders[oldSize], 0, gapSize);
    }
}

CKTextureDesc *CKRasterizerContext::GetTextureData(CKDWORD Texture)
{
    if (Texture >= (CKDWORD)m_Textures.Size())
        return NULL;
    CKTextureDesc *data = m_Textures[Texture];
    if (!data)
        return NULL;
    if ((data->Flags & CKRST_TEXTURE_VALID) == 0)
        return NULL;
    return data;
}

CKBOOL CKRasterizerContext::LoadSprite(CKDWORD Sprite, const VxImageDescEx &SurfDesc)
{
    if (Sprite >= (CKDWORD)m_Sprites.Size())
        return FALSE;

    CKSpriteDesc *sprite = m_Sprites[Sprite];
    if (!sprite)
        return FALSE;

    if (sprite->Textures.IsEmpty())
        return FALSE;

    VxImageDescEx desc = SurfDesc;
    int bytesPerPixel = SurfDesc.BitsPerPixel / 8;

    CKBYTE *image = m_Driver->m_Owner->AllocateObjects((bytesPerPixel * sprite->Textures[0].sw * sprite->Textures[0].sh + sizeof(CKDWORD) - 1) / sizeof(CKDWORD));
    if (!image)
        return FALSE;
    desc.Image = image;

    for (XArray<CKSPRTextInfo>::Iterator it = sprite->Textures.Begin(); it != sprite->Textures.End(); ++it)
    {
        image = desc.Image;

        int spriteBytesPerLine = it->w * bytesPerPixel;
        int textureBytesPerLine = it->sw * bytesPerPixel;

        if (it->w != it->sw || it->h != it->sh)
            memset(image, 0, it->sh * textureBytesPerLine);

        XBYTE *src = &SurfDesc.Image[it->x * bytesPerPixel + it->y * SurfDesc.BytesPerLine];
        for (int h = 0; h < it->h; ++h)
        {
            memcpy(image, src, spriteBytesPerLine);
            image += textureBytesPerLine;
            src += SurfDesc.BytesPerLine;
        }

        desc.BytesPerLine = textureBytesPerLine;
        desc.Width = it->sw;
        desc.Height = it->sh;
        LoadTexture(it->IndexTexture, desc);
    }

    return TRUE;
}

CKSpriteDesc *CKRasterizerContext::GetSpriteData(CKDWORD Sprite)
{
    if (Sprite >= (CKDWORD)m_Sprites.Size())
        return NULL;
    CKSpriteDesc *data = m_Sprites[Sprite];
    if (!data)
        return NULL;
    if ((data->Flags & CKRST_TEXTURE_VALID) == 0)
        return NULL;
    return data;
}

CKVertexBufferDesc *CKRasterizerContext::GetVertexBufferData(CKDWORD VB)
{
    if (VB >= (CKDWORD)m_VertexBuffers.Size())
        return NULL;
    CKVertexBufferDesc *data = m_VertexBuffers[VB];
    if (!data)
        return NULL;
    if ((data->m_Flags & CKRST_VB_VALID) == 0)
        return NULL;
    return data;
}

CKBOOL CKRasterizerContext::TransformVertices(int VertexCount, VxTransformData *Data)
{
    if (!Data->InVertices)
        return FALSE;

    unsigned int offscreen = 0;
    UpdateMatrices(WORLD_TRANSFORM);

    VxVector4 *outVertices = (VxVector4 *)Data->OutVertices;
    unsigned int outStride = Data->OutStride;
    if (!outVertices)
    {
        outVertices = (VxVector4 *)m_Driver->m_Owner->AllocateObjects((VertexCount * sizeof(VxVector4)) / sizeof(CKDWORD));
        outStride = sizeof(VxVector4);
    }

    VxStridedData out(outVertices, outStride);
    VxStridedData in(Data->InVertices, Data->InStride);
    Vx3DMultiplyMatrixVector4Strided(&out, &in, m_TotalMatrix, VertexCount);

    // Store original pointer to reset later
    VxVector4 *originalOutVertices = outVertices;

    if (Data->ClipFlags)
    {
        offscreen = 0xFFFFFFFF;
        for (int v = 0; v < VertexCount; ++v)
        {
            unsigned int clipFlag = 0;

            float w = outVertices->w;
            if (-w > outVertices->x)
                clipFlag |= VXCLIP_LEFT;
            if (outVertices->x > w)
                clipFlag |= VXCLIP_RIGHT;
            if (-w > outVertices->y)
                clipFlag |= VXCLIP_BOTTOM;
            if (outVertices->y > w)
                clipFlag |= VXCLIP_TOP;
            if (outVertices->z < 0.0f)
                clipFlag |= VXCLIP_FRONT;
            if (outVertices->z > w)
                clipFlag |= VXCLIP_BACK;

            offscreen &= clipFlag;
            Data->ClipFlags[v] = clipFlag;

            // Properly advance pointer by stride bytes
            outVertices = (VxVector4*)((CKBYTE*)outVertices + outStride);
        }
    }

    VxVector4 *screenVertices = (VxVector4 *)Data->ScreenVertices;
    if (screenVertices)
    {
        // Reset to original position before second loop
        outVertices = originalOutVertices;

        float halfWidth = m_ViewportData.ViewWidth * 0.5f;
        float halfHeight = m_ViewportData.ViewHeight * 0.5f;
        float centerX = m_ViewportData.ViewX + halfWidth;
        float centerY = m_ViewportData.ViewY + halfHeight;

        for (int v = 0; v < VertexCount; ++v)
        {
            // Check for division by zero
            if (fabs(outVertices->w) > EPSILON)
            {
                float w = 1.0f / outVertices->w;
                screenVertices->w = w;
                screenVertices->z = w * outVertices->z;
                screenVertices->y = centerY - outVertices->y * w * halfHeight;
                screenVertices->x = centerX + outVertices->x * w * halfWidth;
            }
            else
            {
                // Handle the case where w is zero or very small
                screenVertices->w = 0.0f;
                screenVertices->z = 0.0f;
                screenVertices->y = centerY;
                screenVertices->x = centerX;
            }

            // Properly advance pointers by stride bytes
            outVertices = (VxVector4*)((CKBYTE*)outVertices + outStride);
            screenVertices = (VxVector4*)((CKBYTE*)screenVertices + Data->ScreenStride);
        }
    }

    Data->m_Offscreen = offscreen & VXCLIP_ALL;
    return TRUE;
}

CKDWORD CKRasterizerContext::ComputeBoxVisibility(const VxBbox &box, CKBOOL World, VxRect *extents)
{
    UpdateMatrices(World ? VIEW_TRANSFORM : WORLD_TRANSFORM);

    VXCLIP_FLAGS orClipFlags, andClipFlags;
    if (extents)
    {
        VxRect screen(
            (float)m_ViewportData.ViewX,
            (float)m_ViewportData.ViewY,
            (float)(m_ViewportData.ViewX + m_ViewportData.ViewWidth),
            (float)(m_ViewportData.ViewY + m_ViewportData.ViewHeight));
        if (World)
            VxTransformBox2D(m_ViewProjMatrix, box, &screen, extents, orClipFlags, andClipFlags);
        else
            VxTransformBox2D(m_TotalMatrix, box, &screen, extents, orClipFlags, andClipFlags);
    }
    else
    {
        if (World)
            VxTransformBox2D(m_ViewProjMatrix, box, NULL, NULL, orClipFlags, andClipFlags);
        else
            VxTransformBox2D(m_TotalMatrix, box, NULL, NULL, orClipFlags, andClipFlags);
    }

    if (andClipFlags & VXCLIP_ALL)
        return CBV_OFFSCREEN;
    else if (orClipFlags & VXCLIP_ALL)
        return CBV_VISIBLE;
    else
        return CBV_ALLINSIDE;
}

void CKRasterizerContext::InitDefaultRenderStatesValue()
{
    m_StateCache[VXRENDERSTATE_SHADEMODE].DefaultValue = VXSHADE_GOURAUD;
    m_StateCache[VXRENDERSTATE_SRCBLEND].DefaultValue = VXBLEND_ONE;
    m_StateCache[VXRENDERSTATE_ALPHAFUNC].DefaultValue = VXCMP_ALWAYS;
    m_StateCache[VXRENDERSTATE_STENCILFUNC].DefaultValue = VXCMP_ALWAYS;
    m_StateCache[VXRENDERSTATE_STENCILMASK].DefaultValue = 0xFFFFFFFF;
    m_StateCache[VXRENDERSTATE_STENCILWRITEMASK].DefaultValue = 0xFFFFFFFF;
    m_StateCache[VXRENDERSTATE_ANTIALIAS].DefaultValue = FALSE;
    m_StateCache[VXRENDERSTATE_TEXTUREPERSPECTIVE].DefaultValue = FALSE;
    m_StateCache[VXRENDERSTATE_ZENABLE].DefaultValue = TRUE;
    m_StateCache[VXRENDERSTATE_FILLMODE].DefaultValue = VXFILL_SOLID;
    m_StateCache[VXRENDERSTATE_LINEPATTERN].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_ZWRITEENABLE].DefaultValue = TRUE;
    m_StateCache[VXRENDERSTATE_ALPHATESTENABLE].DefaultValue = FALSE;
    m_StateCache[VXRENDERSTATE_DESTBLEND].DefaultValue = VXBLEND_ZERO;
    m_StateCache[VXRENDERSTATE_CULLMODE].DefaultValue = VXCULL_CCW;
    m_StateCache[VXRENDERSTATE_ZFUNC].DefaultValue = VXCMP_LESSEQUAL;
    m_StateCache[VXRENDERSTATE_ALPHAREF].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_DITHERENABLE].DefaultValue = FALSE;
    m_StateCache[VXRENDERSTATE_ALPHABLENDENABLE].DefaultValue = FALSE;
    m_StateCache[VXRENDERSTATE_FOGENABLE].DefaultValue = FALSE;
    m_StateCache[VXRENDERSTATE_SPECULARENABLE].DefaultValue = FALSE;
    m_StateCache[VXRENDERSTATE_FOGCOLOR].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_FOGSTART].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_FOGEND].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_FOGDENSITY].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_EDGEANTIALIAS].DefaultValue = FALSE;
    m_StateCache[VXRENDERSTATE_ZBIAS].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_RANGEFOGENABLE].DefaultValue = FALSE;
    m_StateCache[VXRENDERSTATE_STENCILENABLE].DefaultValue = FALSE;
    m_StateCache[VXRENDERSTATE_STENCILFAIL].DefaultValue = VXSTENCILOP_KEEP;
    m_StateCache[VXRENDERSTATE_STENCILZFAIL].DefaultValue = VXSTENCILOP_KEEP;
    m_StateCache[VXRENDERSTATE_STENCILPASS].DefaultValue = VXSTENCILOP_KEEP;
    m_StateCache[VXRENDERSTATE_STENCILREF].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_TEXTUREFACTOR].DefaultValue = A_MASK;
    m_StateCache[VXRENDERSTATE_WRAP0].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_WRAP1].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_WRAP2].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_WRAP3].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_WRAP4].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_WRAP5].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_WRAP6].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_WRAP7].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_CLIPPING].DefaultValue = TRUE;
    m_StateCache[VXRENDERSTATE_LIGHTING].DefaultValue = TRUE;
    m_StateCache[VXRENDERSTATE_AMBIENT].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_FOGVERTEXMODE].DefaultValue = VXFOG_NONE;
    m_StateCache[VXRENDERSTATE_FOGPIXELMODE].DefaultValue = VXFOG_NONE;
    m_StateCache[VXRENDERSTATE_COLORVERTEX].DefaultValue = FALSE;
    m_StateCache[VXRENDERSTATE_LOCALVIEWER].DefaultValue = TRUE;
    m_StateCache[VXRENDERSTATE_NORMALIZENORMALS].DefaultValue = TRUE;
    m_StateCache[VXRENDERSTATE_CLIPPLANEENABLE].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_INVERSEWINDING].DefaultValue = FALSE;
    m_StateCache[VXRENDERSTATE_TEXTURETARGET].DefaultValue = 0;
}

CKIndexBufferDesc *CKRasterizerContext::GetIndexBufferData(CKDWORD IB)
{
    if (IB >= (CKDWORD)m_IndexBuffers.Size())
        return NULL;
    CKIndexBufferDesc *data = m_IndexBuffers[IB];
    if (!data)
        return NULL;
    if ((data->m_Flags & CKRST_VB_VALID) == 0)
        return NULL;
    return data;
}

CKBOOL CKRasterizerContext::CreateSprite(CKDWORD Sprite, CKSpriteDesc *DesiredFormat)
{
    if (Sprite >= (CKDWORD)m_Sprites.Size() || !DesiredFormat)
        return FALSE;

    CKDWORD width = DesiredFormat->Format.Width;
    CKDWORD height = DesiredFormat->Format.Height;

    short minWidth = (short)m_Driver->m_3DCaps.MinTextureWidth;
    short minHeight = (short)m_Driver->m_3DCaps.MinTextureHeight;

    const short maxWidth = (short)m_Driver->m_3DCaps.MaxTextureWidth;
    const short maxHeight = (short)m_Driver->m_3DCaps.MaxTextureHeight;

    short texWidth = GetPow2(width);
    short texHeight = GetPow2(height);

    if (minWidth < 8)
        minWidth = 8;

    CKSPRTextInfo wti[16] = {};
    int wc = 1;

    if (width < minWidth)
    {
        wti[0].x = 0;
        wti[0].w = (short)width;
        wti[0].sw = minWidth;
    }
    else if (width < maxWidth)
    {
        wti[0].x = 0;
        wti[0].w = (short)width;
        wti[0].sw = texWidth;
    }
    else if (width == maxWidth)
    {
        wti[0].x = 0;
        wti[0].w = maxWidth;
        wti[0].sw = maxWidth;
    }
    else
    {
        wc = 0;
        CKDWORD x = 0;
        CKDWORD w = width;
        for (CKSPRTextInfo *pti = &wti[0]; w >= minWidth && wc < 16; ++pti)
        {
            pti->x = (short)x;
            pti->w = maxWidth;
            pti->sw = maxWidth;

            x += maxWidth;
            w -= maxWidth;
            ++wc;
        }

        if (w > 0)
        {
            wti[wc].x = (short)x;
            wti[wc].w = (short)w;
            wti[wc].sw = minWidth;
            ++wc;
        }
    }

    CKSPRTextInfo hti[16] = {};
    int hc = 1;

    CKDWORD maxRatio = m_Driver->m_3DCaps.MaxTextureRatio;
    if (maxRatio != 0)
    {
        short h = (short)(wti[0].sw / maxRatio);
        if (minHeight < h)
            minHeight = h;
    }

    if (height < minHeight)
    {
        hti[0].y = 0;
        hti[0].h = (short)height;
        hti[0].sh = minHeight;
    }
    else if (height < maxHeight)
    {
        hti[0].y = 0;
        hti[0].h = (short)height;
        hti[0].sh = texHeight;
    }
    else if (height == maxHeight)
    {
        hti[0].y = 0;
        hti[0].h = maxHeight;
        hti[0].sh = maxHeight;
    }
    else
    {
        hc = 0;
        CKDWORD y = 0;
        CKDWORD h = height;
        for (CKSPRTextInfo *pti = &hti[0]; h >= minHeight && hc < 16; ++pti)
        {
            pti->y = (short)y;
            pti->h = maxHeight;
            pti->sh = maxHeight;

            y += maxHeight;
            h -= maxHeight;
            ++hc;
        }

        if (h > 0)
        {
            hti[hc].y = (short)y;
            hti[hc].h = (short)h;
            hti[hc].sh = minHeight;
            ++hc;
        }
    }

    CKSpriteDesc *sprite = m_Sprites[Sprite];
    if (sprite)
        delete sprite;

    sprite = new CKSpriteDesc;
    sprite->Textures.Resize(wc * hc);
    sprite->Textures.Memset(0);
    sprite->Owner = m_Driver->m_Owner;
    m_Sprites[Sprite] = sprite;

    for (int j = 0; j < hc; ++j)
    {
        for (int i = 0; i < wc; ++i)
        {
            CKSPRTextInfo *info = &sprite->Textures[j * wc + i];
            info->x = wti[i].x;
            info->w = wti[i].w;
            info->sw = wti[i].sw;
            info->y = hti[j].y;
            info->h = hti[j].h;
            info->sh = hti[j].sh;
            info->IndexTexture = m_Driver->m_Owner->CreateObjectIndex(CKRST_OBJ_TEXTURE);
            DesiredFormat->Format.Width = info->sw;
            DesiredFormat->Format.Height = info->sh;
            CreateObject(info->IndexTexture, CKRST_OBJ_TEXTURE, DesiredFormat);
        }
    }

    sprite->Flags |= CKRST_TEXTURE_SPRITE;
    sprite->Format.Width = width;
    sprite->Format.Height = height;
    sprite->MipMapCount = 0;

    CKTextureDesc *tex = m_Textures[sprite->Textures[0].IndexTexture];
    if (!tex)
        return FALSE;

    sprite->Flags = tex->Flags;
    sprite->Format = tex->Format;
    sprite->Format.Width = width;
    sprite->Format.Height = height;
    return TRUE;
}

void CKRasterizerContext::UpdateMatrices(CKDWORD Flags)
{
    if ((Flags & m_MatrixUptodate) == 0)
    {
        if ((Flags & WORLD_TRANSFORM) != 0)
            Vx3DMultiplyMatrix4(m_TotalMatrix, m_ProjectionMatrix, m_ModelViewMatrix);
        if ((Flags & VIEW_TRANSFORM) != 0)
            Vx3DMultiplyMatrix4(m_ViewProjMatrix, m_ProjectionMatrix, m_ViewMatrix);
        m_MatrixUptodate |= Flags;
    }
}

CKDWORD CKRasterizerContext::GetDynamicVertexBuffer(CKDWORD VertexFormat, CKDWORD VertexCount, CKDWORD VertexSize, CKDWORD AddKey)
{
    if (VertexFormat == 0 || VertexCount == 0 || VertexSize == 0)
        return 0;

    // Check if hardware supports vertex buffers
    if (!(m_Driver->m_3DCaps.CKRasterizerSpecificCaps & CKRST_SPECIFICCAPS_CANDOVERTEXBUFFER))
        return 0;

    // Generate a unique index based on vertex format properties
    // This ensures different types of vertex data get different buffers
    CKDWORD index = 0;
    // Extract position and normal flags
    index = VertexFormat & (CKRST_VF_POSITIONMASK | CKRST_VF_NORMAL);
    // Incorporate diffuse, specular and texture coord flags with proper shifting
    index |= (VertexFormat & (CKRST_VF_DIFFUSE | CKRST_VF_SPECULAR | CKRST_VF_TEXMASK)) >> 3;
    // Shift down to make room for AddKey
    index >>= 2;
    // Incorporate AddKey into high bits
    index |= AddKey << 7;
    // Ensure index is not zero
    index += 1;

    // Sanity check to make sure index is within valid range
    if (index >= (CKDWORD)m_VertexBuffers.Size())
    {
        // If index is out of bounds, fall back to a default index
        index = 1; // Just use the first VB slot as fallback
    }

    CKVertexBufferDesc *vb = m_VertexBuffers[index];
    if (!vb || vb->m_MaxVertexCount < VertexCount || vb->m_VertexFormat != VertexFormat)
    {
        // Clean up existing vertex buffer if it exists
        if (vb)
        {
            delete vb;
            m_VertexBuffers[index] = NULL;
        }

        // Initialize new vertex buffer descriptor
        CKVertexBufferDesc nvb;
        nvb.m_Flags = CKRST_VB_WRITEONLY | CKRST_VB_DYNAMIC;

        // If AddKey is non-zero, this buffer might be shared across different types of geometry
        if (AddKey != 0)
            nvb.m_Flags |= CKRST_VB_SHARED;

        nvb.m_VertexFormat = VertexFormat;
        nvb.m_VertexSize = VertexSize;

        // Allocate more than requested to avoid frequent resizing
        // Use at least DEFAULT_VB_SIZE for efficiency
        nvb.m_MaxVertexCount = VertexCount + 100;
        if (nvb.m_MaxVertexCount < DEFAULT_VB_SIZE)
            nvb.m_MaxVertexCount = DEFAULT_VB_SIZE;

        // Create the actual vertex buffer object
        if (!CreateObject(index, CKRST_OBJ_VERTEXBUFFER, &nvb))
            return 0;

        // Verify the vertex buffer was created successfully
        if (!m_VertexBuffers[index])
            return 0;
    }

    return index;
}