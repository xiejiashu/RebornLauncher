#include "framework.h"
#include <objidl.h>
#include <gdiplus.h>
#include "resource.h"
#include "Frame.h"
#include "Unit.h"
#include "Sprite.h"

Frame::Frame(int switchSpeed, bool loop)
	: m_switchSpeed(switchSpeed)
	, m_loop(loop)
	, m_currentFrame(0)
	, m_lastSwitchTime(0)
{
}

Frame::~Frame()
{
}

const Gdiplus::Bitmap* Frame::GetBitmap() const
{
	if(m_bitmapFrame.size() > m_currentFrame)
		return m_bitmapFrame[m_currentFrame];
	return nullptr;
}

void Frame::AddBitmap(const Gdiplus::Bitmap* bitmap)
{
	m_bitmapFrame.push_back(bitmap);
}

void Frame::Update(DWORD currentTime)
{
	if (int(currentTime - m_lastSwitchTime) > m_switchSpeed)
	{
		m_lastSwitchTime = currentTime;
		m_currentFrame++;
		if (m_currentFrame >= m_bitmapFrame.size())
		{
			if (m_loop)
			{
				m_currentFrame = 0;
			}
			else
			{
				m_currentFrame = (int)m_bitmapFrame.size() - 1;
			}
		}

		if (m_Owner && m_bitmapFrame.size() > m_currentFrame)
		{
			m_Owner->SetWidth(((Gdiplus::Bitmap*)m_bitmapFrame[m_currentFrame])->GetWidth());
			m_Owner->SetHeight(((Gdiplus::Bitmap*)m_bitmapFrame[m_currentFrame])->GetHeight());
		}
	}
}

void Frame::SetSwitchSpeed(int switchSpeed)
{
	m_switchSpeed = switchSpeed;
}

int Frame::GetSwitchSpeed() const
{
	return m_switchSpeed;
}

void Frame::SetLoop(bool loop)
{
	m_loop = loop;
}

bool Frame::GetLoop() const
{
	return m_loop;
}

void Frame::SetCurrentFrame(int currentFrame)
{
	m_currentFrame = currentFrame;
}

int Frame::GetCurrentFrame() const
{
	return m_currentFrame;
}

void Frame::SetOwner(Sprite* Owner)
{
	m_Owner = Owner;
}
