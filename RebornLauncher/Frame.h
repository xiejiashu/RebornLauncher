#pragma once
#include <vector>
class Sprite;
class Frame
{
public:
	Frame(int switchSpeed = 100, bool loop = true);
	virtual ~Frame();
public:
	const Gdiplus::Bitmap* GetBitmap() const;
	void AddBitmap(const Gdiplus::Bitmap* bitmap);
	void Update(DWORD currentTime);
	// Seter Getter
	void SetSwitchSpeed(int switchSpeed);
	int GetSwitchSpeed() const;
	void SetLoop(bool loop);
	bool GetLoop() const;
	void SetCurrentFrame(int currentFrame);
	int GetCurrentFrame() const;
	void SetOwner(Sprite* Owner);
private:
	std::vector<const Gdiplus::Bitmap*> m_bitmapFrame;
	// 鍒囨崲閫熷害
	int m_switchSpeed;
	// 涓婃鍒囨崲鐨勬椂闂?
	DWORD m_lastSwitchTime;
	// 褰撳墠甯?
	int m_currentFrame;
	// 鏄惁寰幆
	bool m_loop;

	Sprite* m_Owner;
};