#pragma once
#include <functional>

// ��ǰ��ʾ״̬
enum class SpriteState
{
	// վ��
	Stand,
	Move,
	// ��Ծ
	Jump,
	// max
	Max,
};

// ����
enum class SpriteDirection
{
	// ����
	Left,
	// ����
	Right,
	// ����
	Up,
	// ����
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
	// ����
	void MoveLeft();
	// ����
	void MoveRight();
	// ����
	void MoveUp();
	// ����
	void MoveDown();
	// ֹͣ�ƶ�
	void StopMove();
	// ��Ծ
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
	// ��Ծ�߶�
	int m_jumpHeight;
	// ��Ծ�ٶ�
	int m_jumpSpeed;
	// ��Ծ�����½��ļ��ٶ�
	int m_jumpAcceleration;
	// �ƶ��ٶ�
	int m_moveSpeed;
	// �����ϴε��ƶ��ٶ�
	int m_lastMoveSpeed;
	// �ϴ��ƶ���ʱ��
	DWORD m_lastMoveTime;
	// �ϴ���Ծ��ʱ��
	DWORD m_lastJumpTime;

	// ֡
	Frame* m_bitmapFrame[static_cast<int>(SpriteState::Max)];
	
	// ��ǰ��ʾ״̬
	SpriteState m_state;
	// ��ǰ����
	SpriteDirection m_direction;
};
