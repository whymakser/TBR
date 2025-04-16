// made by fokkonaut

#include "engine/server.h"
#include "game/server/gamecontext.h"
#include "helicopter.h"

bool MovingCircleHitsMovingSegment_Analytical(
	vec2 circleLast, vec2 circleNow, float radius,
	vec2 lineLastA, vec2 lineNowA,
	vec2 lineLastB, vec2 lineNowB)
{
	// Step 1: Relative motion
	vec2 circleVel = circleNow - circleLast;
	vec2 lineVelA = lineNowA - lineLastA;
	vec2 lineVelB = lineNowB - lineLastB;
	vec2 lineVel = (lineVelA + lineVelB) * 0.5f;
	vec2 relVel = circleVel - lineVel;

	vec2 C0 = circleLast;
//	vec2 C1 = C0 + relVel;

	vec2 L0 = lineLastA;
	vec2 L1 = lineLastB;
	vec2 d = L1 - L0;
	vec2 m = C0 - L0;

	float a = dot(relVel, relVel);
	float b = dot(relVel, d);
	float c = dot(d, d);
	float d1 = dot(relVel, m);
	float e = dot(d, m);

	float denom = a * c - b * b;

	float t, s;

	if (denom != 0.0f)
	{
		t = (b * e - c * d1) / denom;
	}
	else
	{
		t = 0.0f; // Parallel
	}

	t = clamp(t, 0.0f, 1.0f);

	// Get point on circle path at time t
	vec2 circlePosAtT = C0 + relVel * t;

	// Find closest point on segment
	float segmentLenSq = dot(d, d);
	if (segmentLenSq != 0.0f)
	{
		s = clamp(dot(circlePosAtT - L0, d) / segmentLenSq, 0.0f, 1.0f);
	}
	else
	{
		s = 0.0f;
	}

	vec2 closestOnSegment = L0 + d * s;

	vec2 diff = circlePosAtT - closestOnSegment;
	float distSq = dot(diff, diff);

	return distSq <= radius * radius;
}

CHelicopter::CHelicopter(CGameWorld *pGameWorld, vec2 Pos)
	: CAdvancedEntity(pGameWorld, CGameWorld::ENTTYPE_HELICOPTER, Pos, vec2(80, 128))
{
	m_AllowVipPlus = false;
	m_Elasticity = 0.f;

	m_InputDirection = 0;
	m_Health = 100.f;
	m_Flipped = false;
	m_Angle = 0.f;
	m_Accel = vec2(0.f, 0.f);

	m_pTurretAttachment = nullptr;

	memset(m_aBonedCharacters, 0, sizeof(m_aBonedCharacters));
	m_BackPropellerRadius = 25.f;
	m_TopPropellerRadius = 100.f;
	m_TopPropellersReset = false;

	InitBody();
	InitPropellers();
	GetFullPropellerPositions(m_LastTopPropellerA, m_LastTopPropellerB);

	// Experimental: Downscale helicopter
	float scaleFactor = 0.9f;
	m_Size *= scaleFactor;
	m_BackPropellerRadius *= scaleFactor;
	m_TopPropellerRadius *= scaleFactor;
	for (SBone& Bone : m_aBones)
		Bone.Scale(scaleFactor);
	//

	GameWorld()->InsertEntity(this);
}

CHelicopter::~CHelicopter()
{
	for (int i = 0; i < NUM_BONES; i++)
		Server()->SnapFreeID(m_aBones[i].m_ID);
	for (int i = 0; i < NUM_TRAILS; i++)
		Server()->SnapFreeID(m_aTrails[i].m_ID);
	DestroyTurret();
}

void CHelicopter::Reset()
{
	Dismount();
	GameWorld()->DestroyEntity(this);
}

bool CHelicopter::AttachTurret(CHelicopterTurret *helicopterTurret)
{
	DestroyTurret();

	if (helicopterTurret == nullptr) // Just remove the current turret
		return true;

	if (helicopterTurret->TryBindHelicopter(this)) // Attempt to pass ownership
	{
		m_pTurretAttachment = helicopterTurret;
		PutTurretToForeground();
		return true;
	}
	return false;
}

void CHelicopter::DestroyTurret()
{
	delete m_pTurretAttachment;
	m_pTurretAttachment = nullptr;
}

void CHelicopter::FlingTee(CCharacter *pChar)
{
	if (!pChar->IsAlive())
		return;

	GameServer()->CreateSound(pChar->GetPos(), SOUND_PLAYER_PAIN_SHORT, pChar->TeamMask());
	GameServer()->CreateDeath(pChar->GetPos(), pChar->GetPlayer()->GetCID(), pChar->TeamMask());
	pChar->SetEmote(EMOTE_PAIN, Server()->Tick() + 500 * Server()->TickSpeed() / 1000);

	float helicopterVelocity = length(m_Vel);
	float teeVelocity = length(pChar->GetCore().m_Vel);

	vec2 directionAwayFromBlades = normalize(pChar->m_PrevPos - m_Pos);
	// Known at compile time
	constexpr float teeMass = 10.f;
	constexpr float helicopterMass = 18.f;
	constexpr float transferForceTee = helicopterMass / teeMass; // POOR THING :SKULL: 💀
	constexpr float transferForceHelicopter = teeMass / helicopterMass;
	//
	float totalVelocity = clamp((helicopterVelocity + teeVelocity) * 0.75f, 5.f, 25.f);
	vec2 teeAcceleration = directionAwayFromBlades * transferForceTee * totalVelocity;
	vec2 helicopterAcceleration = -directionAwayFromBlades * transferForceHelicopter * totalVelocity;

	pChar->SetCoreVel(pChar->GetCore().m_Vel + teeAcceleration);
	m_Vel += helicopterAcceleration;
}

void CHelicopter::Tick()
{
	CAdvancedEntity::Tick();
	HandleDropped();

	if (GetOwner())
	{
		GetOwner()->ForceSetPos(m_Pos);
		GetOwner()->Core()->m_Vel = vec2(0, 0);

		m_Gravity = GetOwner()->m_FreezeTime;
		m_GroundVel = GetOwner()->m_FreezeTime || m_Accel.x == 0.f; // when on floor and not moving
	}

	DestroyThingsInItsPath();

	ApplyAcceleration();
	SpinPropellers();

	if (m_pTurretAttachment)
		m_pTurretAttachment->Tick();

	if (m_Health <= 0.f)
		Reset();

	m_PrevPos = m_Pos;
	GetFullPropellerPositions(m_LastTopPropellerA, m_LastTopPropellerB);
}

void CHelicopter::OnInput(CNetObj_PlayerInput *pNewInput)
{
	// Movement controls
	if (!GetOwner() || GetOwner()->m_FreezeTime)
	{
		m_Accel = vec2(0.f, 0.f);
		return;
	}

	m_InputDirection = pNewInput->m_Direction;
	m_Accel.x = (float)pNewInput->m_Direction;

	bool Rise = pNewInput->m_Jump;
	bool Sink = pNewInput->m_Hook;
	if (Rise == Sink)
		m_Accel.y = 0.f;
	else
		m_Accel.y = Rise ? -1 : 1;

	// Weapon controls
	if (m_pTurretAttachment)
		m_pTurretAttachment->OnInput(pNewInput);
}

void CHelicopter::ApplyAcceleration()
{
	float strafeFactor = (m_Flipped == (m_Vel.x > 0.f)) ? 0.4f : 1.f; // Accelerate slower when moving backwards
	m_Vel.x += m_Accel.x * 0.4f * strafeFactor;
	m_Vel.y += m_Accel.y * 0.5f;
	m_Vel.y *= 0.95f;

	// Prevent flipping when not going the opposite direction OR when shooting the opposite direction
	if (((m_InputDirection == -1 && !m_Flipped && m_Vel.x < 0.f) ||
		(m_InputDirection == 1 && m_Flipped && m_Vel.x > 0.f)) &&
		(!m_pTurretAttachment || !m_pTurretAttachment->m_Shooting ||
			(m_Flipped != (m_pTurretAttachment->m_AimPosition.x < 0.f))))
		Flip();

	SetAngle(m_Vel.x);
}

void CHelicopter::DestroyThingsInItsPath()
{
	if (!GetOwner() && IsGrounded())
		return;

	CCharacter *aPossibleCollisions[5];
	int numFound = GameWorld()->FindEntities(m_Pos,
		m_TopPropellerRadius + 200.f,
		(CEntity **)aPossibleCollisions,
		5,
		CGameWorld::ENTTYPE_CHARACTER);
	if (!numFound)
		return;

	for (int i = 0; i < numFound; i++)
	{
		CCharacter *pChar = aPossibleCollisions[i];
		int cID = pChar->GetPlayer()->GetCID();
		if (Server()->Tick() - m_aBonedCharacters[cID] <= 10)
			return;

		vec2 propellerPosA, propellerPosB;
		GetFullPropellerPositions(propellerPosA, propellerPosB);
		bool collisionDetected =
			MovingCircleHitsMovingSegment_Analytical(pChar->m_PrevPos - m_Pos, pChar->GetPos() - m_Pos,
													 pChar->GetProximityRadius(),
													 m_LastTopPropellerA, propellerPosA,
													 m_LastTopPropellerB, propellerPosB);
		if (collisionDetected)
		{
			m_aBonedCharacters[cID] = Server()->Tick();
			FlingTee(pChar);
		}
	}

}

void CHelicopter::GetFullPropellerPositions(vec2& outPosA, vec2& outPosB)
{
	vec2 bladeSpan = normalize(TopPropeller()[0].m_To - TopPropeller()[0].m_From) * m_TopPropellerRadius;
	outPosA = TopPropeller()[0].m_To + bladeSpan;
	outPosB = TopPropeller()[1].m_To - bladeSpan;
}

void CHelicopter::SpinPropellers()
{
	if (Server()->Tick() % 2 != 0)
		return;

	if (!GetOwner() && IsGrounded()) // Reset top propellers when grounded without driver
	{
		if (!m_TopPropellersReset)
			ResetTopPropellers();
		return;
	}

	for (int i = 0; i < NUM_BONES_PROPELLERS_BACK; i++)
		BackPropeller()[i].m_From = rotate_around_point(BackPropeller()[i].m_From, BackPropeller()[i].m_To, 50.f / pi);

	float Len = m_TopPropellerRadius;
	float curLen = clamp((float)sin(Server()->Tick() / 5) * Len, -Len, Len);
	vec2 Diff = TopPropeller()[0].m_From - TopPropeller()[0].m_To;
	Diff = normalize(Diff) * curLen;
	for (int i = 0; i < NUM_BONES_PROPELLERS_TOP; i++)
	{
		TopPropeller()[i].m_From = TopPropeller()[i].m_To + Diff;
		Diff *= -1;
	}

	m_TopPropellersReset = false;
}

bool CHelicopter::Mount(int ClientID)
{
	if (m_Owner != -1)
		return false;

	m_Gravity = false;
	m_GroundVel = false;
	m_Owner = ClientID;
	if (GetOwner())
	{
		GetOwner()->m_pHelicopter = this;
		GetOwner()->SetWeapon(-1);
		GameServer()->SendTuningParams(m_Owner, GetOwner()->m_TuneZone);
	}
	return true;
}

void CHelicopter::Dismount()
{
	if (m_Owner == -1)
		return;

	m_Gravity = true;
	m_GroundVel = true;
	if (GetOwner())
	{
		GetOwner()->m_pHelicopter = nullptr;
		GetOwner()->SetWeapon(GetOwner()->GetLastWeapon());
		GameServer()->SendTuningParams(m_Owner, GetOwner()->m_TuneZone);
	}
	m_Owner = -1;
}

void CHelicopter::Flip()
{
	m_Flipped = !m_Flipped;
	m_Angle *= -1.f;
	for (int i = 0; i < NUM_BONES; i++)
		m_aBones[i].Flip();

	if (m_pTurretAttachment)
		m_pTurretAttachment->SetFlipped(m_Flipped);
}

void CHelicopter::Rotate(float Angle)
{
	m_Angle += Angle;
	for (int i = 0; i < NUM_BONES; i++)
		m_aBones[i].Rotate(Angle);

	if (m_pTurretAttachment)
		m_pTurretAttachment->Rotate(Angle);
}

void CHelicopter::SetAngle(float Angle)
{
	Rotate(-m_Angle);
	Rotate(Angle);
}

void CHelicopter::Snap(int SnappingClient)
{
	if (NetworkClipped(SnappingClient))
		return;

	if (!CmaskIsSet(m_TeamMask, SnappingClient))
		return;

	for (SBone& Bone : m_aBones)
		Bone.Snap(SnappingClient);

	if (GetOwner() || !IsGrounded())
		for (STrail& m_aTrail : m_aTrails)
			m_aTrail.Snap(SnappingClient);

	if (m_pTurretAttachment)
		m_pTurretAttachment->Snap(SnappingClient);
}

void CHelicopter::PutTurretToForeground()
{ // meant for adding turret later, not specifically at initialization
	if (!m_pTurretAttachment)
		return; // what turret?

	int numBonesTurret = m_pTurretAttachment->GetNumBones();

	// Get current IDs
	int *apIDs = new int[NUM_BONES + numBonesTurret];
	for (int i = 0; i < NUM_BONES; i++)
		apIDs[i] = m_aBones[i].m_ID;
	for (int i = 0; i < numBonesTurret; i++)
		apIDs[NUM_BONES + i] = m_pTurretAttachment->Bones()[i].m_ID;
	std::sort(apIDs, apIDs + (NUM_BONES + numBonesTurret));

	// Update sorted IDs (low -> high | ascending)
	for (int i = 0; i < NUM_BONES; i++)
		m_aBones[i].m_ID = apIDs[i];
	for (int i = 0; i < numBonesTurret; i++)
		m_pTurretAttachment->Bones()[i].m_ID = apIDs[NUM_BONES + i];
	delete[] apIDs;
}

void CHelicopter::InitBody()
{
	SBone aBones[NUM_BONES_BODY] = {
		SBone(this, Server()->SnapNewID(), 70, 45, 50, 60),
		SBone(this, Server()->SnapNewID(), -55, 60, 50, 60),
		SBone(this, Server()->SnapNewID(), -25, 40, -30, 60),
		SBone(this, Server()->SnapNewID(), 25, 40, 30, 60),
		SBone(this, Server()->SnapNewID(), 0, -40, 0, -60),
		SBone(this, Server()->SnapNewID(), 35, 40, -35, 40),
		SBone(this, Server()->SnapNewID(), 60, 10, 35, 40),
		SBone(this, Server()->SnapNewID(), 25, -40, 60, 10),
		SBone(this, Server()->SnapNewID(), -35, -40, 25, -40),
		SBone(this, Server()->SnapNewID(), -45, 0, -35, -40),
		SBone(this, Server()->SnapNewID(), -100, 0, -45, 0),
		SBone(this, Server()->SnapNewID(), -120, -30, -100, 0),
		SBone(this, Server()->SnapNewID(), -105, 20, -120, -30),
		SBone(this, Server()->SnapNewID(), -35, 40, -105, 20),
	}; // 0-4: base, 5: propeller bottom, 6-10: body, 11-13: tail
	mem_copy(Body(), aBones, sizeof(SBone) * NUM_BONES_BODY);
}

void CHelicopter::InitPropellers()
{
	float Radius = m_TopPropellerRadius;
	for (int i = 0; i < NUM_BONES_PROPELLERS_TOP; i++)
	{
		TopPropeller()[i] = SBone(this, Server()->SnapNewID(), vec2(Radius, -60.f), vec2(0, -60.f));
		TopPropeller()[i].m_Color = LASERTYPE_DOOR;
		Radius *= -1;
	}

	Radius = m_BackPropellerRadius;
	for (int i = 0; i < NUM_BONES_PROPELLERS_BACK; i++)
	{
		BackPropeller()[i] = SBone(this, Server()->SnapNewID(), vec2(-110.f + Radius, -10.f), vec2(-110.f, -10.f));
		Radius *= -1;
		m_aTrails[i] = STrail(this, Server()->SnapNewID(), &BackPropeller()[i].m_From);
	}
}

void CHelicopter::ResetTopPropellers()
{
	m_TopPropellersReset = true;

	float Radius = m_TopPropellerRadius;
	vec2 Direction = normalize(TopPropeller()[0].m_From - TopPropeller()[0].m_To);
	for (int i = 0; i < NUM_BONES_PROPELLERS_TOP; i++)
	{
		TopPropeller()[i].m_From = TopPropeller()[i].m_To + Direction * Radius;
		Radius *= -1;
	}
}