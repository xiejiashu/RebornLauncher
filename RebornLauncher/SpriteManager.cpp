#include "framework.h"
#include <corecrt_math.h>
#include <objidl.h>
#include <gdiplus.h>
#include "Frame.h"
#include "Unit.h"
#include "Sprite.h"
#include "SpriteManager.h"


SpriteManager::~SpriteManager()
{
}

bool SpriteManager::CreateSprite(std::function<bool(std::shared_ptr<Sprite> pSprite)> CallBack, int x, int y, int width, int height, int jumpHeight, int jumpSpeed, int jumpAcceleration, int moveSpeed)
{
	std::shared_ptr<Sprite> pSprite = std::make_shared<Sprite>(x, y, width, height, jumpHeight, jumpSpeed, jumpAcceleration, moveSpeed);
	if (CallBack(pSprite))
	{
		m_spriteList.push_back(pSprite);
		return true;
	}
	return false;
}

void SpriteManager::Update(DWORD currentTime)
{
	for (auto pSprite : m_spriteList)
	{
		pSprite->Update(currentTime);
	}
}

void SpriteManager::Draw(Gdiplus::Graphics& graphics)
{
	for (auto pSprite : m_spriteList)
	{
		pSprite->Draw(graphics);
	}
}