#pragma once
#include <map>
class ResourceManager
{
public:
	ResourceManager(HINSTANCE hInstance);
	~ResourceManager();
public:
	void LoadAllResource();
	const Gdiplus::Bitmap* GetBitmap(int resId);
	static Gdiplus::Bitmap* LoadPngFromResource(UINT resId, HINSTANCE hInstance);
private:
	std::map<int, Gdiplus::Bitmap*> m_bitmapMap;
	HINSTANCE m_hInstance{nullptr};
};