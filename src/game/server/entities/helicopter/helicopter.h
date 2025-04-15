// made by fokkonaut

#ifndef GAME_SERVER_ENTITIES_HELICOPTER_HELICOPTER_H
#define GAME_SERVER_ENTITIES_HELICOPTER_HELICOPTER_H

#include "helicopter_turret.h"
#include "game/server/entities/advanced_entity.h"

class CHelicopter : public CAdvancedEntity
{
private:
	enum
	{
		NUM_BONES_BODY = 14,
		NUM_BONES_PROPELLERS_TOP = 2,
		NUM_BONES_PROPELLERS_BACK = 2,
		NUM_BONES_PROPELLERS = NUM_BONES_PROPELLERS_TOP + NUM_BONES_PROPELLERS_BACK,
		NUM_BONES = NUM_BONES_BODY + NUM_BONES_PROPELLERS,
		NUM_TRAILS = 2,
		NUM_HELICOPTER_IDS = NUM_BONES + NUM_TRAILS,
	};

	int m_InputDirection;
	float m_Health;

	void InitBody();
	void InitPropellers();
	void SpinPropellers();
	void ResetTopPropellers();

	bool m_Flipped;
	void Flip();

	float m_Angle;
	void Rotate(float Angle);
	void SetAngle(float Angle);

	void ApplyAcceleration();
	vec2 m_Accel;

	int m_aBonedCharacters[MAX_CLIENTS];
	void DestroyThingsInItsPath();

	float m_BackPropellerRadius;
	float m_TopPropellerRadius;
	bool m_TopPropellersReset;
	vec2 m_LastTopPropellerA, m_LastTopPropellerB;
	void GetFullPropellerPositions(vec2& outPosA, vec2& outPosB);

	STrail m_aTrails[NUM_TRAILS];
	SBone m_aBones[NUM_BONES];
	CHelicopterTurret *m_pTurretAttachment;

	SBone *Body() { return &m_aBones[0]; } // size: NUM_BONES_BODY
	SBone *TopPropeller() { return &m_aBones[NUM_BONES_BODY]; } // size: NUM_BONES_PROPELLERS_TOP
	SBone *BackPropeller()
	{
		return &m_aBones[NUM_BONES_BODY + NUM_BONES_PROPELLERS_TOP];
	} // size: NUM_BONES_PROPELLERS_BACK
	void PutTurretToForeground();

public:
	CHelicopter(CGameWorld *pGameWorld, vec2 Pos);
	~CHelicopter() override;

	// Sense
	[[nodiscard]] bool IsFlipped() { return m_Flipped; }
	[[nodiscard]] float Angle() { return m_Angle; }

	// Manipulating
	bool AttachTurret(CHelicopterTurret *helicopterTurret);
	void DestroyTurret();
	void FlingTee(CCharacter *pChar);

	// Ticking
	void Tick() override;
	void Snap(int SnappingClient) override;
	void Reset() override;

	bool Mount(int ClientID);
	void Dismount();
	void OnInput(CNetObj_PlayerInput *pNewInput);
};

#endif // GAME_SERVER_ENTITIES_HELICOPTER_HELICOPTER_H
