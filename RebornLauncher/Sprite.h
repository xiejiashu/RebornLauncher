#pragma once
#include <functional>

enum class SpriteState
{
	Stand,
	Move,
	Jump,
	// max
	Max,
};

enum class SpriteDirection
{
	Left,
	Right,
	Up,
	Down,

	Max
};

class Frame;
class Sprite
	:public Unit
{
public:
	Sprite(int x = 0, int y = 0, int width = 0, int height = 0, int jumpHeight = 0, int jumpSpeed = 0, int jumpAcceleration = 0, int moveSpeed = 0);
	virtual ~Sprite();
public:
	virtual void Update(DWORD currentTime);
	virtual void Draw(Gdiplus::Graphics& graphics);
public:
	void MoveLeft();
	void MoveRight();
	void MoveUp();
	void MoveDown();
	void StopMove();
	void Jump();

	// Setter Getter
	void SetJumpHeight(int jumpHeight);
	int GetJumpHeight() const;
	void SetJumpSpeed(int jumpSpeed);
	int GetJumpSpeed() const;
	void SetJumpAcceleration(int jumpAcceleration);
	int GetJumpAcceleration() const;
	void SetMoveSpeed(int moveSpeed);
	int GetMoveSpeed() const;

	void SetBitmapFrame(SpriteState state, Frame* frame);
	Frame* GetBitmapFrame(SpriteState state) const;
private:
	int m_jumpHeight;
	int m_jumpSpeed;
	int m_jumpAcceleration;
	int m_moveSpeed;
	int m_lastMoveSpeed;
	DWORD m_lastMoveTime;
	DWORD m_lastJumpTime;

	Frame* m_bitmapFrame[static_cast<int>(SpriteState::Max)];
	
	SpriteState m_state;
	SpriteDirection m_direction;
};
