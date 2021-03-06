#include "CKRasterizer.h"

#include <stdio.h>

#ifdef CKNULLRASTERIZER_DLL

CKRasterizer *CKNULLRasterizerStart(WIN_HANDLE AppWnd);
void CKNULLRasterizerClose(CKRasterizer *rst);

PLUGIN_EXPORT void CKRasterizerGetInfo(CKRasterizerInfo *info)
{
    info->StartFct = CKNULLRasterizerStart;
    info->CloseFct = CKNULLRasterizerClose;
    info->Desc = "NULL Rasterizer";
}

CKRasterizer *CKNULLRasterizerStart(WIN_HANDLE AppWnd)
{
    CKRasterizer *rst = new CKRasterizer;
    if (!rst)
        return NULL;

    if (!rst->Start(AppWnd))
    {
        delete rst;
        rst = NULL;
    }

    return rst;
}

void CKNULLRasterizerClose(CKRasterizer *rst)
{
    if (rst)
    {
        rst->Close();
        delete rst;
    }
}

#endif // CKNULLRASTERIZER_DLL

CKRasterizer::CKRasterizer() : m_Objects(),
                               m_ObjectsIndex(),
                               m_OtherRasterizers(),
                               m_ProblematicDrivers(),
                               m_Drivers(),
                               m_FullscreenContext(NULL)
{
    m_ObjectsIndex.Resize(INIT_OBJECTSLOTS);
    m_ObjectsIndex.Fill(0);
    memset(m_ObjectsIndex.Begin(), 4, 256);

    m_FirstFreeIndex[ObjTypeIndex(CKRST_OBJ_TEXTURE)] = 1;
    m_FirstFreeIndex[ObjTypeIndex(CKRST_OBJ_SPRITE)] = 1;
    m_FirstFreeIndex[ObjTypeIndex(CKRST_OBJ_VERTEXBUFFER)] = 256;
    m_FirstFreeIndex[ObjTypeIndex(CKRST_OBJ_INDEXBUFFER)] = 1;
    m_FirstFreeIndex[ObjTypeIndex(CKRST_OBJ_VERTEXSHADER)] = 1;
    m_FirstFreeIndex[ObjTypeIndex(CKRST_OBJ_PIXELSHADER)] = 1;
}

CKRasterizer::~CKRasterizer()
{
    m_FullscreenContext = NULL;
}

CKBOOL CKRasterizer::Start(WIN_HANDLE AppWnd)
{
    m_MainWindow = AppWnd;
    CKRasterizerDriver *driver = new CKRasterizerDriver;
    driver->InitNULLRasterizerCaps(this);
    m_Drivers.PushBack(driver);
    return TRUE;
}

CKDWORD CKRasterizer::CreateObjectIndex(CKRST_OBJECTTYPE Type, CKBOOL WarnOthers)
{
    int objectsIndexCount = m_ObjectsIndex.Size();
    int i;
    for (i = m_FirstFreeIndex[ObjTypeIndex(Type)]; i < objectsIndexCount; ++i)
        if ((Type & m_ObjectsIndex[i]) == 0)
            break;

    if (i > objectsIndexCount)
    {
        m_ObjectsIndex.Resize(2 * i);
        memset(m_ObjectsIndex.At(i), 0, i);
        int driverCount = GetDriverCount();
        for (int d = 0; d < driverCount; d++)
        {
            CKRasterizerDriver *driver = GetDriver(d);
            if (driver)
            {
                for (XArray<CKRasterizerContext *>::Iterator it = driver->m_Contexts.Begin();
                     it != driver->m_Contexts.End(); ++it)
                    (*it)->UpdateObjectArrays(this);
            }
        }
    }

    m_ObjectsIndex[i] |= Type;
    m_FirstFreeIndex[ObjTypeIndex(Type)] = i + 1;

    if (WarnOthers)
        for (CKRasterizer **it = m_OtherRasterizers.Begin(); it != m_OtherRasterizers.End(); ++it)
            (*it)->CreateObjectIndex(Type, FALSE);

    return i;
}

CKBOOL CKRasterizer::ReleaseObjectIndex(CKDWORD ObjectIndex, CKRST_OBJECTTYPE Type, CKBOOL WarnOthers)
{
    if (ObjectIndex > (CKDWORD)m_ObjectsIndex.Size())
        return FALSE;
    if ((m_ObjectsIndex[ObjectIndex] & Type) == 0)
        return FALSE;

    m_ObjectsIndex[ObjectIndex] &= ~Type;

    int driverCount = GetDriverCount();
    for (int d = 0; d < driverCount; d++)
    {
        CKRasterizerDriver *driver = GetDriver(d);
        if (driver)
        {
            for (XArray<CKRasterizerContext *>::Iterator it = driver->m_Contexts.Begin();
                 it != driver->m_Contexts.End(); ++it)
                (*it)->DeleteObject(ObjectIndex, Type);
        }
    }

    if (ObjectIndex < m_FirstFreeIndex[ObjTypeIndex(Type)])
        m_FirstFreeIndex[ObjTypeIndex(Type)] = ObjectIndex;

    if (WarnOthers)
        for (CKRasterizer **it = m_OtherRasterizers.Begin(); it != m_OtherRasterizers.End(); ++it)
            (*it)->ReleaseObjectIndex(ObjectIndex, Type, FALSE);

    return 0;
}

XBYTE *CKRasterizer::AllocateObjects(int size)
{
    m_Objects.Allocate(size);
    return (XBYTE *)m_Objects.Buffer();
}

void CKRasterizer::LinkRasterizer(CKRasterizer *rst)
{
    if (rst != this)
        m_OtherRasterizers.PushBack(rst);
}

void CKRasterizer::RemoveLinkedRasterizer(CKRasterizer *rst)
{
    if (rst != this)
        m_OtherRasterizers.Remove(rst);
}

CKBOOL CKRasterizer::LoadVideoCardFile(const char *FileName)
{
    VxConfiguration config;
    int cline;
    XString error;
    int sectionCount;
    if (!config.BuildFromFile(FileName, cline, error) ||
        (sectionCount = config.GetNumberOfSubSections()) == 0)
        return FALSE;

    ConstSectionIt sectionIt = config.BeginSections();
    for (VxConfigurationSection *section = *sectionIt;
         section != NULL && --sectionCount > 0; section = config.GetNextSection(sectionIt))
    {
        CKDriverProblems driverProblems;

        VxConfigurationEntry *companyEntry = section->GetEntry("Company");
        if (companyEntry)
            driverProblems.m_Vendor = companyEntry->GetValue();

        VxConfigurationEntry *rendererEntry = section->GetEntry("Renderer");
        if (rendererEntry)
            driverProblems.m_Renderer = rendererEntry->GetValue();

        VxConfigurationEntry *exactVersionEntry = section->GetEntry("ExactVersion");
        if (exactVersionEntry)
        {
            driverProblems.m_Version = exactVersionEntry->GetValue();
            driverProblems.m_VersionMustBeExact = TRUE;
        }

        VxConfigurationEntry *upToVersionEntry = section->GetEntry("UpToVersion");
        if (upToVersionEntry)
        {
            driverProblems.m_Version = upToVersionEntry->GetValue();
            driverProblems.m_VersionMustBeExact = FALSE;
        }

        VxConfigurationEntry *deviceDescEntry = section->GetEntry("DeviceDesc");
        if (deviceDescEntry)
            driverProblems.m_DeviceDesc = deviceDescEntry->GetValue();

        VxConfigurationEntry *bugClampToEdgeEntry = section->GetEntry("Bug_ClampEdge");
        if (bugClampToEdgeEntry)
            bugClampToEdgeEntry->GetValueAsInteger(driverProblems.m_ClampToEdgeBug);

        VxConfigurationEntry *onlyIn16Entry = section->GetEntry("OnlyIn16Bpp");
        if (onlyIn16Entry)
            onlyIn16Entry->GetValueAsInteger(driverProblems.m_OnlyIn16);

        VxConfigurationEntry *onlyIn32Entry = section->GetEntry("OnlyIn32Bpp");
        if (onlyIn32Entry)
            onlyIn32Entry->GetValueAsInteger(driverProblems.m_OnlyIn32);

        VxConfigurationEntry *maxTextureWidthEntry = section->GetEntry("MaxTextureWidth");
        if (maxTextureWidthEntry)
            maxTextureWidthEntry->GetValueAsInteger(driverProblems.m_RealMaxTextureWidth);

        VxConfigurationEntry *maxTextureHeightEntry = section->GetEntry("MaxTextureHeight");
        if (maxTextureHeightEntry)
            maxTextureHeightEntry->GetValueAsInteger(driverProblems.m_RealMaxTextureHeight);

        VxConfigurationSection *bugRGBASection = section->GetSubSection("Bug_RGBA");
        if (bugRGBASection)
        {
            VxConfigurationEntry *bug32ARGB8888 = bugRGBASection->GetEntry("_32_ARGB8888");
            if (bug32ARGB8888)
                driverProblems.m_TextureFormatsRGBABug.PushBack(_32_ARGB8888);

            VxConfigurationEntry *bug32RGB888 = bugRGBASection->GetEntry("_32_RGB888");
            if (bug32RGB888)
                driverProblems.m_TextureFormatsRGBABug.PushBack(_32_RGB888);

            VxConfigurationEntry *bug24RGB888 = bugRGBASection->GetEntry("_24_RGB888");
            if (bug24RGB888)
                driverProblems.m_TextureFormatsRGBABug.PushBack(_24_RGB888);

            VxConfigurationEntry *bug16RGB565 = bugRGBASection->GetEntry("_16_RGB565");
            if (bug16RGB565)
                driverProblems.m_TextureFormatsRGBABug.PushBack(_16_RGB565);

            VxConfigurationEntry *bug16RGB555 = bugRGBASection->GetEntry("_16_RGB555");
            if (bug16RGB555)
                driverProblems.m_TextureFormatsRGBABug.PushBack(_16_RGB555);

            VxConfigurationEntry *bug16ARGB1555 = bugRGBASection->GetEntry("_16_ARGB1555");
            if (bug16ARGB1555)
                driverProblems.m_TextureFormatsRGBABug.PushBack(_16_ARGB1555);

            VxConfigurationEntry *bug16ARGB4444 = bugRGBASection->GetEntry("_16_ARGB4444");
            if (bug16ARGB4444)
                driverProblems.m_TextureFormatsRGBABug.PushBack(_16_ARGB4444);

            VxConfigurationEntry *bug8RGB332 = bugRGBASection->GetEntry("_8_RGB332");
            if (bug8RGB332)
                driverProblems.m_TextureFormatsRGBABug.PushBack(_8_RGB332);

            VxConfigurationEntry *bug8ARGB2222 = bugRGBASection->GetEntry("_8_ARGB2222");
            if (bug8ARGB2222)
                driverProblems.m_TextureFormatsRGBABug.PushBack(_8_ARGB2222);

            VxConfigurationEntry *bugDXT1 = bugRGBASection->GetEntry("_DXT1");
            if (bugDXT1)
                driverProblems.m_TextureFormatsRGBABug.PushBack(_DXT1);

            VxConfigurationEntry *bugDXT3 = bugRGBASection->GetEntry("_DXT3");
            if (bugDXT3)
                driverProblems.m_TextureFormatsRGBABug.PushBack(_DXT3);

            VxConfigurationEntry *bugDXT5 = bugRGBASection->GetEntry("_DXT5");
            if (bugDXT5)
                driverProblems.m_TextureFormatsRGBABug.PushBack(_DXT5);
        }

        VxConfigurationSection *OsSection = section->GetSubSection("Os");
        if (OsSection)
        {
            VxConfigurationEntry *win95 = OsSection->GetEntry("VXOS_WIN95");
            if (win95)
                driverProblems.m_ConcernedOS.PushBack(VXOS_WIN95);

            VxConfigurationEntry *win98 = OsSection->GetEntry("VXOS_WIN98");
            if (win98)
                driverProblems.m_ConcernedOS.PushBack(VXOS_WIN98);

            VxConfigurationEntry *winNT4 = OsSection->GetEntry("VXOS_WINNT4");
            if (winNT4)
                driverProblems.m_ConcernedOS.PushBack(VXOS_WINNT4);

            VxConfigurationEntry *win2k = OsSection->GetEntry("VXOS_WIN2K");
            if (win2k)
                driverProblems.m_ConcernedOS.PushBack(VXOS_WIN2K);

            VxConfigurationEntry *winXP = OsSection->GetEntry("VXOS_WINXP");
            if (winXP)
                driverProblems.m_ConcernedOS.PushBack(VXOS_WINXP);

            VxConfigurationEntry *macOS9 = OsSection->GetEntry("VXOS_MACOS9");
            if (macOS9)
                driverProblems.m_ConcernedOS.PushBack(VXOS_MACOS9);

            VxConfigurationEntry *macOSX = OsSection->GetEntry("VXOS_MACOSX");
            if (macOSX)
                driverProblems.m_ConcernedOS.PushBack(VXOS_MACOSX);

            VxConfigurationEntry *linuxX86 = OsSection->GetEntry("VXOS_LINUXX86");
            if (linuxX86)
                driverProblems.m_ConcernedOS.PushBack(VXOS_LINUXX86);
        }

        m_ProblematicDrivers.PushBack(driverProblems);
    }

    return TRUE;
}

CKDriverProblems *
CKRasterizer::FindDriverProblems(const XString &Vendor, const XString &Renderer, const XString &Version,
                                 const XString &DeviceDesc, int Bpp)
{

    if (m_ProblematicDrivers.Size() == 0)
        return NULL;

    for (XClassArray<CKDriverProblems>::Iterator it = m_ProblematicDrivers.Begin();
         it != m_ProblematicDrivers.End(); ++it)
    {
        if (Vendor != "" && it->m_Vendor == Vendor)
        {
            if (it->m_Renderer != "" && it->m_Renderer != Renderer)
                continue;
        }
        else
        {
            if (it->m_DeviceDesc != DeviceDesc)
                continue;
        }

        if (it->m_Version != "" && Version != "")
        {
            if (it->m_VersionMustBeExact)
            {
                if (it->m_Version != Version)
                    continue;
            }
            else
            {
                int major1, minor1, patch1;
                sscanf(it->m_Version.CStr(), "%d.%d.%d", &major1, &minor1, &patch1);
                int major2, minor2, patch2;
                sscanf(Version.CStr(), "%d.%d.%d", &major2, &minor2, &patch2);
                if (major1 > major2 || major1 == major2 && (minor1 > minor2 || minor1 == minor2 && patch1 >= patch2))
                    continue;
            }
        }

        if (it->m_OnlyIn16 && Bpp != 16)
            continue;
        if (it->m_OnlyIn32 && Bpp != 32)
            continue;

        if (!it->m_ConcernedOS.IsHere(VxGetOs()))
            continue;

        return it;
    }

    return NULL;
}

void ConvertAttenuationModelFromDX5(float &_a0, float &_a1, float &_a2, float range)
{
    float a0 = 1.0f / (_a0 + _a1 + _a2);
    float a1 = (_a2 + _a2 + _a1) * (a0 / range) * a0;
    float a2 = a0 * _a2 * a0 / (range * range) + a1 * a1 / a0;
    _a0 = a0;
    _a1 = a1;
    _a2 = a2;
}

CKDWORD CKRSTGetVertexFormat(CKRST_DPFLAGS DpFlags, CKDWORD &VertexSize)
{
    CKDWORD point = 0;
    CKDWORD stage = ObjTypeIndex(DpFlags >> 9);

    if ((DpFlags & CKRST_DP_TRANSFORM) != 0)
    {
        VertexSize = 12;
        if ((DpFlags & CKRST_DP_WEIGHTMASK) != 0)
        {
            CKDWORD weight;
            for (weight = 1; weight <= 5; ++weight)
                if ((DpFlags & (CKRST_DP_WEIGHTS1 << (weight - 1))) != 0)
                {
                    point = 2 * weight + 4;
                    VertexSize = 4 * weight + 12;
                    break;
                }

            if ((DpFlags & CKRST_DP_MATRIXPAL) != 0)
                point |= CKRST_VF_NORMAL;
        }
        else
        {
            point = 2;
        }

        if ((DpFlags & CKRST_VF_POSITION) != 0)
        {
            VertexSize += 12 + 8 * stage;
            point |= CKRST_VF_NORMAL;
            return point | (stage << 8);
        }

        if ((DpFlags & CKRST_DP_DIFFUSE) != 0)
        {
            VertexSize += 4;
            point |= CKRST_VF_DIFFUSE;
        }
    }
    else
    {
        VertexSize = 16;
        point = 4;

        if ((DpFlags & CKRST_DP_DIFFUSE) != 0)
        {
            VertexSize = 20;
            point |= CKRST_VF_DIFFUSE | CKRST_VF_RASTERPOS;
        }

        if ((DpFlags & CKRST_DP_SPECULAR) != 0)
        {
            VertexSize += 4;
            point |= CKRST_VF_SPECULAR;
        }
    }

    VertexSize += 8 * stage;
    return point | (stage << 8);
}

CKDWORD CKRSTGetVertexSize(CKDWORD VertexFormat)
{
    CKDWORD vertexSize;
    switch (VertexFormat & 0xF)
    {
    case CKRST_VF_POSITION:
        vertexSize = 12;
        break;
    case CKRST_VF_RASTERPOS:
    case CKRST_VF_POSITION1W:
        vertexSize = 16;
        break;
    case CKRST_VF_POSITION2W:
        vertexSize = 20;
        break;
    case CKRST_VF_POSITION3W:
        vertexSize = 24;
        break;
    case CKRST_VF_POSITION4W:
        vertexSize = 28;
        break;
    case CKRST_VF_POSITION5W:
        vertexSize = 32;
        break;
    }

    if ((VertexFormat & CKRST_VF_NORMAL) != 0)
        vertexSize += 12;
    if ((VertexFormat & CKRST_VF_DIFFUSE) != 0)
        vertexSize += 4;
    if ((VertexFormat & CKRST_VF_PSIZE) != 0)
        vertexSize += 4;
    if ((VertexFormat & CKRST_VF_SPECULAR) != 0)
        vertexSize += 4;

    CKWORD tex = VertexFormat >> 16;
    if (tex == 0)
    {
        vertexSize += 8 * ((VertexFormat & CKRST_VF_TEXMASK) >> 8);
        return vertexSize;
    }

    if ((VertexFormat & CKRST_VF_TEXMASK) != 0)
    {
        CKDWORD texSize[4] = {8, 12, 16, 4};
        for (int i = 0; i < CKRST_MAX_STAGES - 1; ++i)
        {
            vertexSize += texSize[tex & 3];
            tex >>= 2;
        }
    }

    return vertexSize;
}

CKBYTE *CKRSTLoadVertexBuffer(CKBYTE *VBMem, CKDWORD VFormat, CKDWORD VSize, VxDrawPrimitiveData *data)
{
    CKBYTE *ptr = (CKBYTE *)data->PositionPtr;

    if (VFormat == CKRST_VF_VERTEX &&
        VSize == 32 &&
        data->PositionStride == 32 &&
        data->NormalStride == 32 &&
        data->TexCoordStride == 32 &&
        data->NormalPtr == ptr + 12 &&
        data->TexCoordPtr == ptr + 24)
    {
        memcpy(VBMem, ptr, 32 * data->VertexCount);
        return &VBMem[32 * data->VertexCount];
    }

    int offset;
    if ((VFormat & CKRST_VF_RASTERPOS) != 0)
    {
        VxCopyStructure(data->VertexCount, VBMem, VSize, 16, data->PositionPtr, data->PositionStride);
        offset = 16;
    }
    else
    {
        VxCopyStructure(data->VertexCount, VBMem, VSize, 12, data->PositionPtr, data->PositionStride);
        offset = 12;
    }

    if ((VFormat & CKRST_VF_NORMAL) != 0)
    {
        if (data->NormalPtr)
            VxCopyStructure(data->VertexCount, &VBMem[offset], VSize, 12, data->NormalPtr, data->NormalStride);
        offset += 12;
    }

    if ((VFormat & CKRST_VF_DIFFUSE) != 0)
    {
        if (data->ColorPtr)
        {
            VxCopyStructure(data->VertexCount, &VBMem[offset], VSize, 4, data->ColorPtr, data->ColorStride);
        }
        else
        {
            int src = -1;
            VxFillStructure(data->VertexCount, &VBMem[offset], VSize, 4, &src);
        }
        offset += 4;
    }

    if ((VFormat & CKRST_VF_SPECULAR) != 0)
    {
        if (data->SpecularColorPtr)
        {
            VxCopyStructure(data->VertexCount, &VBMem[offset], VSize, 4, data->SpecularColorPtr, data->SpecularColorStride);
        }
        else
        {
            int src = 0;
            VxFillStructure(data->VertexCount, &VBMem[offset], VSize, 4, &src);
        }
        offset += 4;
    }

    if ((VFormat & CKRST_VF_TEXMASK) != 0)
    {
        if (data->TexCoordPtr)
            VxCopyStructure(data->VertexCount, &VBMem[offset], VSize, 8, data->TexCoordPtr, data->TexCoordStride);
        offset += 8;
    }

    if ((VFormat & CKRST_VF_TEXMASK) > CKRST_VF_TEX1)
        for (int i = 0; i < CKRST_MAX_STAGES - 1; ++i)
        {
            VxCopyStructure(data->VertexCount, &VBMem[offset], VSize, 8, data->TexCoordPtrs[i], data->TexCoordStrides[i]);
            offset += 8;
        }

    return &VBMem[VSize * data->VertexCount];
}

void CKRSTSetupDPFromVertexBuffer(CKBYTE *VBMem, CKVertexBufferDesc *VB, VxDrawPrimitiveData &DpData)
{
    DpData.PositionPtr = VBMem;
    DpData.PositionStride = VB->m_VertexSize;

    CKBYTE *ptr;
    if ((VB->m_VertexFormat & CKRST_VF_POSITION) != 0)
        ptr = VBMem + 12;
    else
        ptr = VBMem + 16;

    if ((VB->m_VertexFormat & CKRST_VF_NORMAL) != 0)
    {
        DpData.NormalPtr = ptr;
        DpData.NormalStride = VB->m_VertexSize;
        ptr += 12;
    }
    else
    {
        DpData.NormalPtr = NULL;
        DpData.NormalStride = 0;
    }

    if ((VB->m_VertexFormat & CKRST_VF_DIFFUSE) != 0)
    {
        DpData.ColorPtr = ptr;
        DpData.ColorStride = VB->m_VertexSize;
        ptr += 4;
    }
    else
    {
        DpData.ColorPtr = NULL;
        DpData.ColorStride = 0;
    }

    if ((VB->m_VertexFormat & CKRST_VF_SPECULAR) != 0)
    {
        DpData.SpecularColorPtr = ptr;
        DpData.SpecularColorStride = VB->m_VertexSize;
        ptr += 4;
    }
    else
    {
        DpData.SpecularColorPtr = NULL;
        DpData.SpecularColorStride = 0;
    }

    DpData.TexCoordPtr = ptr;
    DpData.TexCoordStride = VB->m_VertexSize;
    ptr += 8;

    memset(DpData.TexCoordPtrs, NULL, sizeof(DpData.TexCoordPtrs));
    memset(DpData.TexCoordStrides, 0, sizeof(DpData.TexCoordStrides));
    if ((VB->m_VertexFormat & CKRST_VF_TEXMASK) > CKRST_VF_TEX1)
        for (int i = 0; i < CKRST_MAX_STAGES - 1; ++i)
        {
            DpData.TexCoordPtrs[i] = ptr;
            DpData.TexCoordStrides[i] = VB->m_VertexSize;
            ptr += 8;
        }
}