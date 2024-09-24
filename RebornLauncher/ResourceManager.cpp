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
	// 加载UI

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
        // 懒加载
		Gdiplus::Bitmap* pBitmap = LoadPngFromResource(resId, m_hInstance);
		if (pBitmap)
		{
			m_bitmapMap[resId] = pBitmap;
			return pBitmap;
		}
	}
	return nullptr;
}

// 从资源中加载 PNG 到 GDI+ 的 Bitmap
Gdiplus::Bitmap* ResourceManager::LoadPngFromResource(UINT resId,HINSTANCE hInstance) {
    Gdiplus::Bitmap* pBitmap = nullptr;

    // 1. 查找资源
    HRSRC hResource = FindResource(hInstance, MAKEINTRESOURCE(resId), L"PNG");
    if (!hResource) {
        return nullptr;  // 未找到资源
    }

    // 2. 获取资源大小并加载资源
    DWORD imageSize = SizeofResource(hInstance, hResource);
    HGLOBAL hGlobal = LoadResource(hInstance, hResource);
    if (!hGlobal) {
        return nullptr;
    }

    // 3. 锁定资源获取指向 PNG 数据的指针
    void* pResourceData = LockResource(hGlobal);
    if (!pResourceData) {
        return nullptr;
    }

    // 4. 将资源数据拷贝到全局内存
    HGLOBAL hBuffer = GlobalAlloc(GMEM_MOVEABLE, imageSize);
    if (hBuffer) {
        void* pBuffer = GlobalLock(hBuffer);
        memcpy_s(pBuffer, imageSize, pResourceData, imageSize);
        GlobalUnlock(hBuffer);

        // 5. 创建 IStream 以便 GDI+ 使用
        IStream* pStream = nullptr;
        if (CreateStreamOnHGlobal(hBuffer, TRUE, &pStream) == S_OK) {
            // 6. 使用 GDI+ Bitmap 构造函数从流中加载 PNG
            pBitmap = new Gdiplus::Bitmap(pStream);
            if (pBitmap->GetLastStatus() != Gdiplus::Ok) {
                delete pBitmap;
                pBitmap = nullptr;
            }
            pStream->Release();  // 释放 IStream
        }

        // 7. 释放全局内存
        GlobalFree(hBuffer);
    }

    return pBitmap;
}