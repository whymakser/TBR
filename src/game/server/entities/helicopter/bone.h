//
// Created by Matq on 11/04/2025.
//

#ifndef GAME_SERVER_ENTITIES_HELICOPTER_BONE_H
#define GAME_SERVER_ENTITIES_HELICOPTER_BONE_H

#include "../../entity.h"

struct SBone
{
	CEntity *m_pEntity;
	int m_ID;
	vec2 m_From;
	vec2 m_To;
	int m_Color;

	SBone() : SBone(nullptr, -1, 0, 0, 0, 0) { }
	SBone(CEntity *pEntity, int SnapID, float FromX, float FromY, float ToX, float ToY)
		: SBone(pEntity, SnapID, vec2(FromX, FromY), vec2(ToX, ToY)) { }
	SBone(CEntity *pEntity, int SnapID, vec2 From, vec2 To)
		: m_pEntity(pEntity), m_ID(SnapID), m_From(From), m_To(To), m_Color(LASERTYPE_RIFLE) { }

	// Sense
	IServer *Server() { return m_pEntity->Server(); }
	CGameContext *GameServer() { return m_pEntity->GameServer(); }
	float GetLength() { return distance(m_To, m_From); }

	// Manipulating
	void AssignEntityAndID(CEntity* pEntity, int SnapID)
	{
		m_pEntity = pEntity;
		m_ID = SnapID;
	}
	void UpdateLine(vec2 From, vec2 To)
	{
		m_From = From;
		m_To = To;
	}
	void Flip()
	{
		m_From.x *= -1;
		m_To.x *= -1;
	}
	void Rotate(float Angle)
	{
		m_From = rotate(m_From, Angle);
		m_To = rotate(m_To, Angle);
	}
	void Scale(float factor)
	{
		m_From *= factor;
		m_To *= factor;
	}

	// Ticking
	void Snap(int SnappingClient);
};

struct STrail
{
	CEntity *m_pEntity;
	int m_ID;
	vec2 *m_pPos;

public:
	STrail() : m_pEntity(nullptr), m_ID(-1), m_pPos(nullptr) { };
	STrail(CEntity *pEntity, int SnapID, vec2 *pPos) : m_pEntity(pEntity), m_ID(SnapID), m_pPos(pPos) { };

	// Sense
	IServer *Server() { return m_pEntity->Server(); }

	// Ticking
	void Snap(int SnappingClient);
};

#endif // GAME_SERVER_ENTITIES_HELICOPTER_BONE_H