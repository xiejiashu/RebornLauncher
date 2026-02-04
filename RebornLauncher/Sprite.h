#pragma once
#include <functional>

// 褰撳墠鏄剧ず鐘舵€?
enum class SpriteState
{
	// 绔欑珛
	Stand,
	Move,
	// 璺宠穬
	Jump,
	// max
	Max,
};

// 鏂瑰悜
enum class SpriteDirection
{
	// 鍚戝乏
	Left,
	// 鍚戝彸
	Right,
	// 鍚戜笂
	Up,
	// 鍚戜笅
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
	// 宸︾Щ
	void MoveLeft();
	// 鍙崇Щ
	void MoveRight();
	// 涓婄Щ
	void MoveUp();
	// 涓嬬Щ
	void MoveDown();
	// 鍋滄绉诲姩
	void StopMove();
	// 璺宠穬
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
	// 璺宠穬楂樺害
	int m_jumpHeight;
	// 璺宠穬閫熷害
	int m_jumpSpeed;
	// 璺宠穬涓婂崌涓嬮檷鐨勫姞閫熷害
	int m_jumpAcceleration;
	// 绉诲姩閫熷害
	int m_moveSpeed;
	// 淇濆瓨涓婃鐨勭Щ鍔ㄩ€熷害
	int m_lastMoveSpeed;
	// 涓婃绉诲姩鐨勬椂闂?
	DWORD m_lastMoveTime;
	// 涓婃璺宠穬鐨勬椂闂?
	DWORD m_lastJumpTime;

	// 甯?
	Frame* m_bitmapFrame[static_cast<int>(SpriteState::Max)];
	
	// 褰撳墠鏄剧ず鐘舵€?
	SpriteState m_state;
	// 褰撳墠鏂瑰悜
	SpriteDirection m_direction;
};
