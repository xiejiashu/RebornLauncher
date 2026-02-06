#include "framework.h"
#include <corecrt_math.h>
#include <objidl.h>
#include <gdiplus.h>
#include "Frame.h"
#include "Unit.h"
#include "Sprite.h"
#include "ResourceManager.h"

ResourceManager::~ResourceManager()
{
	for (auto it : m_bitmapMap)
	{
		if (it.second)
		{
			DeleteObject(it.second);
		}
	}
}

ResourceManager::ResourceManager(HINSTANCE hInstance)
	: m_hInstance(hInstance)
{

}

void ResourceManager::LoadAllResource()
{

}

const Gdiplus::Bitmap* ResourceManager::GetBitmap(int resId)
{
	auto it = m_bitmapMap.find(resId);
	if (it != m_bitmapMap.end())
	{
		return it->second;
	}
	else
	{
		Gdiplus::Bitmap* pBitmap = LoadPngFromResource(resId, m_hInstance);
		if (pBitmap)
		{
			m_bitmapMap[resId] = pBitmap;
			return pBitmap;
		}
	}
	return nullptr;
}

Gdiplus::Bitmap* ResourceManager::LoadPngFromResource(UINT resId,HINSTANCE hInstance) {
    Gdiplus::Bitmap* pBitmap = nullptr;

    HRSRC hResource = FindResource(hInstance, MAKEINTRESOURCE(resId), L"PNG");
    if (!hResource) {
        return nullptr;
    }

    DWORD imageSize = SizeofResource(hInstance, hResource);
    HGLOBAL hGlobal = LoadResource(hInstance, hResource);
    if (!hGlobal) {
        return nullptr;
    }

    void* pResourceData = LockResource(hGlobal);
    if (!pResourceData) {
        return nullptr;
    }

    HGLOBAL hBuffer = GlobalAlloc(GMEM_MOVEABLE, imageSize);
    if (hBuffer) {
        void* pBuffer = GlobalLock(hBuffer);
        memcpy_s(pBuffer, imageSize, pResourceData, imageSize);
        GlobalUnlock(hBuffer);

        IStream* pStream = nullptr;
        if (CreateStreamOnHGlobal(hBuffer, TRUE, &pStream) == S_OK) {
            pBitmap = new Gdiplus::Bitmap(pStream);
            if (pBitmap->GetLastStatus() != Gdiplus::Ok) {
                delete pBitmap;
                pBitmap = nullptr;
            }
            pStream->Release();
        }

        GlobalFree(hBuffer);
    }

    return pBitmap;
}