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
	// 鍔犺浇UI

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
        // 鎳掑姞杞?
		Gdiplus::Bitmap* pBitmap = LoadPngFromResource(resId, m_hInstance);
		if (pBitmap)
		{
			m_bitmapMap[resId] = pBitmap;
			return pBitmap;
		}
	}
	return nullptr;
}

// 浠庤祫婧愪腑鍔犺浇 PNG 鍒?GDI+ 鐨?Bitmap
Gdiplus::Bitmap* ResourceManager::LoadPngFromResource(UINT resId,HINSTANCE hInstance) {
    Gdiplus::Bitmap* pBitmap = nullptr;

    // 1. 鏌ユ壘璧勬簮
    HRSRC hResource = FindResource(hInstance, MAKEINTRESOURCE(resId), L"PNG");
    if (!hResource) {
        return nullptr;  // 鏈壘鍒拌祫婧?
    }

    // 2. 鑾峰彇璧勬簮澶у皬骞跺姞杞借祫婧?
    DWORD imageSize = SizeofResource(hInstance, hResource);
    HGLOBAL hGlobal = LoadResource(hInstance, hResource);
    if (!hGlobal) {
        return nullptr;
    }

    // 3. 閿佸畾璧勬簮鑾峰彇鎸囧悜 PNG 鏁版嵁鐨勬寚閽?
    void* pResourceData = LockResource(hGlobal);
    if (!pResourceData) {
        return nullptr;
    }

    // 4. 灏嗚祫婧愭暟鎹嫹璐濆埌鍏ㄥ眬鍐呭瓨
    HGLOBAL hBuffer = GlobalAlloc(GMEM_MOVEABLE, imageSize);
    if (hBuffer) {
        void* pBuffer = GlobalLock(hBuffer);
        memcpy_s(pBuffer, imageSize, pResourceData, imageSize);
        GlobalUnlock(hBuffer);

        // 5. 鍒涘缓 IStream 浠ヤ究 GDI+ 浣跨敤
        IStream* pStream = nullptr;
        if (CreateStreamOnHGlobal(hBuffer, TRUE, &pStream) == S_OK) {
            // 6. 浣跨敤 GDI+ Bitmap 鏋勯€犲嚱鏁颁粠娴佷腑鍔犺浇 PNG
            pBitmap = new Gdiplus::Bitmap(pStream);
            if (pBitmap->GetLastStatus() != Gdiplus::Ok) {
                delete pBitmap;
                pBitmap = nullptr;
            }
            pStream->Release();  // 閲婃斁 IStream
        }

        // 7. 閲婃斁鍏ㄥ眬鍐呭瓨
        GlobalFree(hBuffer);
    }

    return pBitmap;
}