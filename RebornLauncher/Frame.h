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
	// �л��ٶ�
	int m_switchSpeed;
	// �ϴ��л���ʱ��
	DWORD m_lastSwitchTime;
	// ��ǰ֡
	int m_currentFrame;
	// �Ƿ�ѭ��
	bool m_loop;

	Sprite* m_Owner;
};