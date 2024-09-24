#pragma once
#include <vector>
#include <memory>
#include <functional>
class SpriteManager
{
public:
	~SpriteManager();
public:
	bool CreateSprite(std::function<bool(std::shared_ptr<Sprite> pSprite)> CallBack, int x = 0, int y = 0, int width = 0, int height = 0, int jumpHeight = 0, int jumpSpeed = 0, int jumpAcceleration = 0, int moveSpeed = 0);
public:
	void Update(DWORD currentTime);
	void Draw(Gdiplus::Graphics& graphics);
private:
	std::vector<std::shared_ptr<Sprite>> m_spriteList;
};