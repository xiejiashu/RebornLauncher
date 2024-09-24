#pragma once
#include <functional>

// 当前显示状态
enum class SpriteState
{
	// 站立
	Stand,
	Move,
	// 跳跃
	Jump,
	// max
	Max,
};

// 方向
enum class SpriteDirection
{
	// 向左
	Left,
	// 向右
	Right,
	// 向上
	Up,
	// 向下
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
	// 左移
	void MoveLeft();
	// 右移
	void MoveRight();
	// 上移
	void MoveUp();
	// 下移
	void MoveDown();
	// 停止移动
	void StopMove();
	// 跳跃
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
	// 跳跃高度
	int m_jumpHeight;
	// 跳跃速度
	int m_jumpSpeed;
	// 跳跃上升下降的加速度
	int m_jumpAcceleration;
	// 移动速度
	int m_moveSpeed;
	// 保存上次的移动速度
	int m_lastMoveSpeed;
	// 上次移动的时间
	DWORD m_lastMoveTime;
	// 上次跳跃的时间
	DWORD m_lastJumpTime;

	// 帧
	Frame* m_bitmapFrame[static_cast<int>(SpriteState::Max)];
	
	// 当前显示状态
	SpriteState m_state;
	// 当前方向
	SpriteDirection m_direction;
};
