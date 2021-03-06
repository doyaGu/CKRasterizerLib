#include "CKRasterizer.h"

CKDWORD GetMsb(CKDWORD data, CKDWORD index)
{
#define OPERAND_SIZE (sizeof(CKDWORD) * 8)
    CKDWORD i = OPERAND_SIZE - 1;
#ifdef WIN32
    __asm
    {
        mov eax, data
        bsr eax, eax
        mov i, eax
    }
#else
    if (data != 0)
        while (!(data & (1 << (OPERAND_SIZE - 1))))
        {
            data <<= 1;
            --i;
        }
#endif
    return (i > index) ? index : i;
#undef OPERAND_SIZE
}

CKDWORD GetLsb(CKDWORD data, CKDWORD index)
{
    CKDWORD i = 0;
#ifdef WIN32
    __asm
    {
        mov eax, data
        bsf eax, eax
        mov i, eax
    }
#else
    if (data != 0)
        while (!(data & 1))
        {
            data >>= 1;
            ++i;
        }
#endif
    return (i > index) ? index : i;
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
    m_Owner = NULL;
    m_PosX = 0;
    m_PosY = 0;
    m_Width = 0;
    m_RefreshRate = 0;
    m_PixelFormat = UNKNOWN_PF;
    m_Fullscreen = 0;
    m_SceneBegined = 0;
    m_MatrixUptodate = 0;
    m_TransparentMode = 0;
    m_Height = 0;
    m_Bpp = 0;
    m_ZBpp = 0;
    m_StencilBpp = 0;

    m_TotalMatrix = VxMatrix::Identity();
    m_WorldMatrix = VxMatrix::Identity();
    m_ViewMatrix = VxMatrix::Identity();
    m_ProjectionMatrix = VxMatrix::Identity();

    m_Textures.Resize(INIT_OBJECTSLOTS + 1);
    m_Sprites.Resize(INIT_OBJECTSLOTS + 1);
    m_VertexBuffers.Resize(INIT_OBJECTSLOTS + 1);
    m_IndexBuffers.Resize(INIT_OBJECTSLOTS + 1);
    m_VertexShaders.Resize(INIT_OBJECTSLOTS + 1);
    m_PixelShaders.Resize(INIT_OBJECTSLOTS + 1);

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
    if (ObjIndex >= (CKDWORD)m_Textures.Size())
        return FALSE;

    switch (Type)
    {
    case CKRST_OBJ_TEXTURE:
        if (m_Textures[ObjIndex])
            delete m_Textures[ObjIndex];
        m_Textures[ObjIndex] = NULL;
        break;
    case CKRST_OBJ_SPRITE:
        if (m_Sprites[ObjIndex])
            delete m_Sprites[ObjIndex];
        m_Sprites[ObjIndex] = NULL;
        break;
    case CKRST_OBJ_VERTEXBUFFER:
        if (m_VertexBuffers[ObjIndex])
            delete m_VertexBuffers[ObjIndex];
        m_VertexBuffers[ObjIndex] = NULL;
        break;
    case CKRST_OBJ_INDEXBUFFER:
        if (m_IndexBuffers[ObjIndex])
            delete m_IndexBuffers[ObjIndex];
        m_IndexBuffers[ObjIndex] = NULL;
        break;
    case CKRST_OBJ_VERTEXSHADER:
        if (m_VertexShaders[ObjIndex])
            delete m_VertexShaders[ObjIndex];
        m_VertexShaders[ObjIndex] = NULL;
        break;
    case CKRST_OBJ_PIXELSHADER:
        if (m_PixelShaders[ObjIndex])
            delete m_PixelShaders[ObjIndex];
        m_PixelShaders[ObjIndex] = NULL;
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
    int size = rst->m_ObjectsIndex.Size();
    int oldSize = m_Textures.Size();

    m_Textures.Resize(size + 1);
    m_Sprites.Resize(size + 1);
    m_VertexBuffers.Resize(size + 1);
    m_IndexBuffers.Resize(size + 1);
    m_VertexShaders.Resize(size + 1);
    m_PixelShaders.Resize(size + 1);

    int len = (size - oldSize) * sizeof(void *);
    memset(&m_Textures[oldSize], 0, len);
    memset(&m_Sprites[oldSize], 0, len);
    memset(&m_VertexBuffers[oldSize], 0, len);
    memset(&m_IndexBuffers[oldSize], 0, len);
    memset(&m_VertexShaders[oldSize], 0, len);
    memset(&m_PixelShaders[oldSize], 0, len);
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
    if ((sprite->Textures.Size() & ~0xF) == 0)
        return FALSE;

    VxImageDescEx surface = SurfDesc;
    int nb = SurfDesc.BitsPerPixel / 8;
    surface.Image = m_Driver->m_Owner->AllocateObjects(sprite->Textures[0].sw * sprite->Textures[0].sh);
    if (!surface.Image)
        return FALSE;

    for (XArray<CKSPRTextInfo>::Iterator it = sprite->Textures.Begin(); it != sprite->Textures.End(); ++it)
    {
        int wb = it->w * nb;
        int swb = it->sw * nb;

        XBYTE *pi = &SurfDesc.Image[it->y * SurfDesc.BytesPerLine + it->x * nb];
        XBYTE *pb = surface.Image;

        if (it->w != it->sw || it->h != it->sh)
        {
            int sz = swb * it->sh;
            memset(pb, 0, sz);
            memset(&pb[sz], 0, sz & 3);
        }

        for (int h = 0; h < it->h; ++h)
        {
            memcpy(pb, pi, wb);
            pb += swb;
            pi += SurfDesc.BytesPerLine;
        }

        surface.BytesPerLine = swb;
        surface.Width = it->sw;
        surface.Height = it->sh;
        LoadTexture(it->IndexTexture, surface);
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
    UpdateMatrices(1);

    VxVector4 *outVertices = (VxVector4 *)Data->OutVertices;
    unsigned int outStride = Data->OutStride;
    if (!outVertices)
    {
        outVertices = (VxVector4 *)m_Driver->m_Owner->AllocateObjects(2 * VertexCount * sizeof(void *));
        outStride = 32;
    }

    VxVector *inVertices = (VxVector *)Data->InVertices;
    unsigned int inStride = Data->InStride;

    VxStridedData out(outVertices, outStride);
    VxStridedData in(inVertices, inStride);
    Vx3DMultiplyMatrixVector4Strided(&out, &in, m_TotalMatrix, VertexCount);

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
            outVertices += outStride;
        }
    }

    VxVector4 *screenVertices = (VxVector4 *)Data->ScreenVertices;
    if (screenVertices)
    {
        float halfWidth = m_ViewportData.ViewWidth * 0.5f;
        float halfHeight = m_ViewportData.ViewHeight * 0.5f;
        float centerX = m_ViewportData.ViewX + halfWidth;
        float centerY = m_ViewportData.ViewY + halfHeight;
        for (int v = 0; v < VertexCount; ++v)
        {
            float w = 1.0f / outVertices->w;
            screenVertices->w = w;
            screenVertices->z = w * outVertices->z;
            screenVertices->y = centerY - outVertices->y * w * halfHeight;
            screenVertices->x = centerX + outVertices->x * w * halfWidth;
            screenVertices += Data->ScreenStride;
        }
    }

    Data->m_Offscreen = offscreen & VXCLIP_ALL;
    return TRUE;
}

CKDWORD CKRasterizerContext::ComputeBoxVisibility(const VxBbox &box, CKBOOL World, VxRect *extents)
{
    CKDWORD flags = (World) ? WORLD_TRANSFORM : VIEW_TRANSFORM;
    UpdateMatrices(flags);

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

    if ((andClipFlags & VXCLIP_ALL) != 0)
        return 0;
    else if ((orClipFlags & VXCLIP_ALL) != 0)
        return VXCLIP_BOXLEFT;
    else
        return VXCLIP_BOXBOTTOM;
}

void CKRasterizerContext::InitDefaultRenderStatesValue()
{
    m_StateCache[VXRENDERSTATE_SHADEMODE].DefaultValue = 2;
    m_StateCache[VXRENDERSTATE_SRCBLEND].DefaultValue = 2;
    m_StateCache[VXRENDERSTATE_ALPHAFUNC].DefaultValue = 8;
    m_StateCache[VXRENDERSTATE_STENCILFUNC].DefaultValue = 8;
    m_StateCache[VXRENDERSTATE_STENCILMASK].DefaultValue = -1;
    m_StateCache[VXRENDERSTATE_STENCILWRITEMASK].DefaultValue = -1;
    m_StateCache[VXRENDERSTATE_ANTIALIAS].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_TEXTUREPERSPECTIVE].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_ZENABLE].DefaultValue = 1;
    m_StateCache[VXRENDERSTATE_FILLMODE].DefaultValue = 3;
    m_StateCache[VXRENDERSTATE_LINEPATTERN].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_ZWRITEENABLE].DefaultValue = 1;
    m_StateCache[VXRENDERSTATE_ALPHATESTENABLE].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_DESTBLEND].DefaultValue = 1;
    m_StateCache[VXRENDERSTATE_CULLMODE].DefaultValue = 3;
    m_StateCache[VXRENDERSTATE_ZFUNC].DefaultValue = 4;
    m_StateCache[VXRENDERSTATE_ALPHAREF].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_DITHERENABLE].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_ALPHABLENDENABLE].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_FOGENABLE].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_SPECULARENABLE].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_FOGCOLOR].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_FOGSTART].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_FOGEND].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_FOGDENSITY].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_EDGEANTIALIAS].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_ZBIAS].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_RANGEFOGENABLE].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_STENCILENABLE].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_STENCILFAIL].DefaultValue = 1;
    m_StateCache[VXRENDERSTATE_STENCILZFAIL].DefaultValue = 1;
    m_StateCache[VXRENDERSTATE_STENCILPASS].DefaultValue = 1;
    m_StateCache[VXRENDERSTATE_STENCILREF].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_TEXTUREFACTOR].DefaultValue = 0xFF000000;
    m_StateCache[VXRENDERSTATE_WRAP0].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_WRAP1].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_WRAP2].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_WRAP3].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_WRAP4].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_WRAP5].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_WRAP6].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_WRAP7].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_CLIPPING].DefaultValue = 1;
    m_StateCache[VXRENDERSTATE_LIGHTING].DefaultValue = 1;
    m_StateCache[VXRENDERSTATE_AMBIENT].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_FOGVERTEXMODE].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_FOGPIXELMODE].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_COLORVERTEX].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_LOCALVIEWER].DefaultValue = 1;
    m_StateCache[VXRENDERSTATE_NORMALIZENORMALS].DefaultValue = 1;
    m_StateCache[VXRENDERSTATE_CLIPPLANEENABLE].DefaultValue = 0;
    m_StateCache[VXRENDERSTATE_INVERSEWINDING].DefaultValue = 0;
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
    if (Sprite > (CKDWORD)m_Sprites.Size() || !DesiredFormat)
        return FALSE;

    CKSPRTextInfo texInfo[16] = {};
    int wc, hc;

    short width = (short)DesiredFormat->Format.Width;
    short minTextureWidth = (short)m_Driver->m_3DCaps.MinTextureWidth;
    if (minTextureWidth < 8)
        minTextureWidth = 8;

    short maxTextureWidthMsb = (short)GetMsb(m_Driver->m_3DCaps.MaxTextureWidth, 32);
    short widthMsb = (short)GetMsb(width, maxTextureWidthMsb);
    short widthLsb = (short)GetLsb(width, maxTextureWidthMsb);

    if (width < minTextureWidth)
    {
        texInfo[0].x = 0;
        texInfo[0].w = width;
        texInfo[0].sw = minTextureWidth;
        wc = 1;
    }
    else if (widthMsb == widthLsb && 1 << widthMsb == width)
    {
        texInfo[0].x = 0;
        texInfo[0].w = (short)(1 << widthMsb);
        texInfo[0].sw = (short)(1 << widthMsb);
        wc = 1;
    }
    else if (widthMsb + 1 <= maxTextureWidthMsb && ((1 << (widthMsb + 1)) - width) <= 32)
    {
        texInfo[0].x = 0;
        texInfo[0].w = width;
        texInfo[0].sw = (short)(1 << (widthMsb + 1));
        wc = 1;
    }
    else
    {
        short x = 0;
        short w = width;
        for (wc = 0; wc < 15; ++wc)
        {
            texInfo[wc].x = x;
            texInfo[wc].w = (short)(1 << widthMsb);
            texInfo[wc].sw = (short)(1 << widthMsb);
            x += (short)(1 << widthMsb);
            w -= (short)(1 << widthMsb);
            widthMsb = (short)GetMsb(w, maxTextureWidthMsb);
            if (w < minTextureWidth)
                break;
        }
        if (w > 0)
        {
            texInfo[wc].x = x;
            texInfo[wc].w = w;
            texInfo[wc].sw = minTextureWidth;
            ++wc;
        }
    }

    short height = (short)DesiredFormat->Format.Height;
    short minTextureHeight = (short)m_Driver->m_3DCaps.MinTextureHeight;
    CKDWORD maxTextureRatio = m_Driver->m_3DCaps.MaxTextureRatio;
    if (maxTextureRatio != 0)
    {
        if (minTextureHeight < (short)(texInfo[0].sw / maxTextureRatio))
            minTextureHeight = (short)(texInfo[0].sw / maxTextureRatio);
    }

    short maxTextureHeightMsb = (short)GetMsb(m_Driver->m_3DCaps.MaxTextureHeight, 32);
    short heightMsb = (short)GetMsb(height, maxTextureHeightMsb);
    short heightLsb = (short)GetLsb(height, maxTextureHeightMsb);

    if (height < minTextureHeight)
    {
        texInfo[0].y = 0;
        texInfo[0].h = height;
        texInfo[0].sh = minTextureHeight;
        hc = 1;
    }
    else if (heightMsb == heightLsb && (1 << heightMsb) == height)
    {
        texInfo[0].y = 0;
        texInfo[0].h = (short)(1 << heightMsb);
        texInfo[0].sh = (short)(1 << heightMsb);
        hc = 1;
    }
    else if (heightMsb + 1 <= maxTextureHeightMsb && ((1 << (heightMsb + 1)) - height) > 32)
    {
        texInfo[0].y = 0;
        texInfo[0].h = height;
        texInfo[0].sh = (short)(1 << (heightMsb + 1));
        hc = 1;
    }
    else
    {
        short y = 0;
        short h = height;
        for (hc = 0; hc < 15; ++hc)
        {
            texInfo[hc].y = y;
            texInfo[hc].h = (short)(1 << heightMsb);
            texInfo[hc].sh = (short)(1 << heightMsb);
            y += (short)(1 << heightMsb);
            h -= (short)(1 << heightMsb);
            heightMsb = (short)GetMsb(h, maxTextureHeightMsb);
            if (h < minTextureHeight)
                break;
        }
        if (h > 0)
        {
            texInfo[hc].y = y;
            texInfo[hc].h = h;
            texInfo[hc].sh = minTextureHeight;
            ++hc;
        }
    }

    CKSpriteDesc *sprite = m_Sprites[Sprite];
    if (sprite)
        delete sprite;

    sprite = new CKSpriteDesc;
    sprite->Textures.Reserve(wc * hc + 1);
    sprite->Textures.Memset(0);

    for (int j = 0; j < hc; ++j)
    {
        for (int i = 0; i < wc; ++i)
        {
            CKSPRTextInfo *info = &sprite->Textures[j * i + i];
            info->IndexTexture = m_Owner->CreateObjectIndex(CKRST_OBJ_TEXTURE);
            info->x = texInfo[i].x;
            info->y = texInfo[j].y;
            info->w = texInfo[i].w;
            info->h = texInfo[j].h;
            info->sw = texInfo[i].sw;
            info->sh = texInfo[j].sh;
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

    sprite->Format.Set(tex->Format);
    sprite->Flags = tex->Flags;
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