#include "framework.h"
#include <objidl.h>
#include <gdiplus.h>
#include "resource.h"
#include "Unit.h"

Unit::Unit(int x, int y, int width, int height)
	: m_x(x)
	, m_y(y)
	, m_width(width)
	, m_height(height)
{
}

Unit::~Unit()
{
}

void Unit::Draw(Gdiplus::Graphics& graphics)
{
}

int Unit::GetX() const
{
	return m_x;
}

void Unit::SetX(int x)
{
	m_x = x;
}

int Unit::GetY() const
{
	return m_y;
}

void Unit::SetY(int y)
{
	m_y = y;
}

int Unit::GetWidth() const
{
	return m_width;
}

void Unit::SetWidth(int width)
{
	m_width = width;
}

int Unit::GetHeight() const
{
	return m_height;
}

void Unit::SetHeight(int height)
{
	m_height = height;
}