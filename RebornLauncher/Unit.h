#pragma once
class Unit
{
public:
	Unit(int x = 0, int y = 0, int width = 0, int height = 0);
	virtual ~Unit();
public:
	virtual void Draw(Gdiplus::Graphics& graphics);
public:
	// Getter Setter
	int GetX() const;
	void SetX(int x);
	int GetY() const;
	void SetY(int y);
	int GetWidth() const;
	void SetWidth(int width);
	int GetHeight() const;
	void SetHeight(int height);
private:
	int m_x;
	int m_y;
	int m_width;
	int m_height;
};