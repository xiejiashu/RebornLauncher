#include "framework.h"
#include <objidl.h>
#include <gdiplus.h>
#include "resource.h"
#include "Frame.h"
#include "Unit.h"
#include "Sprite.h"

using namespace Gdiplus;

Sprite::Sprite(int x, int y, int width, int height, int jumpHeight, int jumpSpeed, int jumpAcceleration, int moveSpeed)
	: Unit(x, y, width, height)
	, m_jumpHeight(jumpHeight)
	, m_jumpSpeed(jumpSpeed)
	, m_jumpAcceleration(jumpAcceleration)
	, m_moveSpeed(moveSpeed)
	, m_bitmapFrame()
	, m_state(SpriteState::Stand)
	, m_lastMoveTime(0)
	, m_lastJumpTime(0)
	, m_direction(SpriteDirection::Right)
	, m_lastMoveSpeed(moveSpeed)
{
}

Sprite::~Sprite()
{
	for (auto frame : m_bitmapFrame)
	{
		if (frame != nullptr)
			delete frame;
	}
}

void Sprite::Update(DWORD currentTime)
{
	if (m_state == SpriteState::Move)
	{
		DWORD deltaTime = currentTime - m_lastMoveTime;
		SetX(GetX() + m_moveSpeed * deltaTime / 1000);
		m_lastMoveTime = currentTime;
	}
	else if (m_state == SpriteState::Jump)
	{
		DWORD deltaTime = currentTime - m_lastJumpTime;
		SetY(GetY() - (m_jumpSpeed * deltaTime / 1000 - m_jumpAcceleration * deltaTime / 1000 * deltaTime / 1000 / 2));
		m_jumpSpeed -= m_jumpAcceleration * deltaTime / 1000;
		if (GetY() >= 0)
		{
			SetY(0);
			m_state = SpriteState::Stand;
		}
		m_lastJumpTime = currentTime;
	}

	Frame* frame = m_bitmapFrame[static_cast<int>(m_state)];
	if (frame)
	{
		frame->Update(currentTime);
	}
}

void Sprite::Draw(Graphics& graphics)
{
	Frame* frame = m_bitmapFrame[static_cast<int>(m_state)];

	if (frame)
	{
		const Bitmap* bitmap = frame->GetBitmap();
		if (bitmap)
		{
			if (m_direction == SpriteDirection::Left)
			{
				Gdiplus::Matrix matrix;
				matrix.Translate((GetX() + GetWidth()) * 1.f, GetY() * 1.f);
				matrix.Scale(-1, 1);
				graphics.SetTransform(&matrix);
			}
			else if (m_direction == SpriteDirection::Up)
			{
				Gdiplus::Matrix matrix;
				matrix.Translate(GetX() + GetWidth() / 2.f, GetY() + GetHeight() / 2.f);
				matrix.RotateAt(270.f, PointF(GetX() + GetWidth() / 2.f, GetY() + GetHeight() / 2.f));
				graphics.SetTransform(&matrix);
			}
			else if (m_direction == SpriteDirection::Down)
			{
				Gdiplus::Matrix matrix;
				matrix.Translate(GetX() + GetWidth() / 2.f, GetY() + GetHeight() / 2.f);
				matrix.RotateAt(90.f, PointF(GetX() + GetWidth() / 2.f, GetY() + GetHeight() / 2.f));
				graphics.SetTransform(&matrix);
			}
			graphics.DrawImage((Bitmap*)bitmap, GetX(), GetY(), GetWidth(), GetHeight());
			graphics.ResetTransform();
		}
	}
}

void Sprite::MoveLeft()
{
	m_state = SpriteState::Move;
	m_direction = SpriteDirection::Left;
}

void Sprite::MoveRight()
{
	m_state = SpriteState::Move;
	m_direction = SpriteDirection::Right;
}

void Sprite::MoveUp()
{
	m_state = SpriteState::Move;
	m_direction = SpriteDirection::Up;
}

void Sprite::MoveDown()
{
	m_state = SpriteState::Move;
	m_direction = SpriteDirection::Down;
}

void Sprite::StopMove()
{
	m_state = SpriteState::Stand;
	m_lastMoveSpeed = m_moveSpeed;
	m_moveSpeed = 0;
}

void Sprite::Jump()
{
	m_state = SpriteState::Jump;
}

void Sprite::SetJumpHeight(int jumpHeight)
{
	m_jumpHeight = jumpHeight;
}

int Sprite::GetJumpHeight() const
{
	return m_jumpHeight;
}

void Sprite::SetJumpSpeed(int jumpSpeed)
{
	m_jumpSpeed = jumpSpeed;
}

int Sprite::GetJumpSpeed() const
{
	return m_jumpSpeed;
}

void Sprite::SetJumpAcceleration(int jumpAcceleration)
{
	m_jumpAcceleration = jumpAcceleration;
}

int Sprite::GetJumpAcceleration() const
{
	return m_jumpAcceleration;
}

void Sprite::SetMoveSpeed(int moveSpeed)
{
	m_moveSpeed = moveSpeed;
}

int Sprite::GetMoveSpeed() const
{
	return m_moveSpeed;
}

void Sprite::SetBitmapFrame(SpriteState state, Frame* frame)
{
	if(m_bitmapFrame[static_cast<int>(state)])
		delete m_bitmapFrame[static_cast<int>(state)];
	m_bitmapFrame[static_cast<int>(state)] = frame;
	frame->SetOwner(this);
}

Frame* Sprite::GetBitmapFrame(SpriteState state) const
{
	return m_bitmapFrame[static_cast<int>(state)];
}