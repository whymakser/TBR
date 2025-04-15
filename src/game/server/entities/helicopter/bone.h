//
// Created by Matq on 11/04/2025.
//

#ifndef GAME_SERVER_ENTITIES_HELICOPTER_BONE_H
#define GAME_SERVER_ENTITIES_HELICOPTER_BONE_H

#include "../../entity.h"

struct SBone
{
	vec2 m_From;
	vec2 m_To;
	int m_ID;
	int m_Color;

	SBone() : SBone(0, 0, 0, 0) { }
	SBone(float FromX, float FromY, float ToX, float ToY) : SBone(vec2(FromX, FromY), vec2(ToX, ToY)) { }
	SBone(vec2 From, vec2 To) : m_From(From), m_To(To), m_ID(-1), m_Color(LASERTYPE_RIFLE) { }

	// Sense
	[[nodiscard]] float GetLength() { return distance(m_To, m_From); }

	// Manipulating
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
	void Snap(int SnappingClient, CEntity *followParent);
};

struct STrail
{
	vec2 *m_pPos;
	int m_ID;

	void Snap(CEntity *followParent);
};

#endif // GAME_SERVER_ENTITIES_HELICOPTER_BONE_H