/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITY_H
#define GAME_SERVER_ENTITY_H

#include <base/vmath.h>

#include "alloc.h"
#include "gameworld.h"

/*
	Class: Entity
		Basic entity class.
*/
class CEntity
{
	MACRO_ALLOC_HEAP()

private:
	/* Friend classes */
	friend class CGameWorld; // for entity list handling

	/* Identity */
	class CGameWorld *m_pGameWorld;

	CEntity *m_pPrevTypeEntity;
	CEntity *m_pNextTypeEntity;

	int m_ID;
	int m_ObjType;

	/*
		Variable: m_ProximityRadius
			Contains the physical size of the entity.
	*/
	float m_ProximityRadius;

	/* State */
	bool m_MarkedForDestroy;

protected:
	/* State */

	/*
		Variable: m_Pos
			Contains the current posititon of the entity.
	*/
	vec2 m_Pos;

	/* Getters */
	int GetID() const					{ return m_ID; }

public:
	/* Constructor */
	CEntity(CGameWorld *pGameWorld, int Objtype, vec2 Pos, int ProximityRadius = 0, bool Collision = true);

	/* Destructor */
	virtual ~CEntity();

	/* Objects */
	class CGameWorld *GameWorld()		{ return m_pGameWorld; }
	class CConfig *Config()				{ return m_pGameWorld->Config(); }
	class CGameContext *GameServer()	{ return m_pGameWorld->GameServer(); }
	class IServer *Server()				{ return m_pGameWorld->Server(); }

	/* Getters */
	CEntity *TypeNext()					{ return m_pNextTypeEntity; }
	CEntity *TypePrev()					{ return m_pPrevTypeEntity; }
	const vec2 &GetPos() const			{ return m_Pos; }
	float GetProximityRadius() const	{ return m_ProximityRadius; }
	bool IsMarkedForDestroy() const		{ return m_MarkedForDestroy; }

	/* Setters */
	void MarkForDestroy()				{ m_MarkedForDestroy = true; }

	/* Other functions */

	/*
		Function: Destroy
			Destroys the entity.
	*/
	virtual void Destroy() { delete this; }

	/*
		Function: Reset
			Called when the game resets the map. Puts the entity
			back to its starting state or perhaps destroys it.
	*/
	virtual void Reset() {}

	/*
		Function: Tick
			Called to progress the entity to the next tick. Updates
			and moves the entity to its new state and position.
	*/
	virtual void Tick() {}

	/*
		Function: TickDeferred
			Called after all entities Tick() function has been called.
	*/
	virtual void TickDeferred() {}

	/*
		Function: TickPaused
			Called when the game is paused, to freeze the state and position of the entity.
	*/
	virtual void TickPaused() {}

	/*
		Function: Snap
			Called when a new snapshot is being generated for a specific
			client.

		Arguments:
			SnappingClient - ID of the client which snapshot is
				being generated. Could be -1 to create a complete
				snapshot of everything in the game for demo
				recording.
	*/
	virtual void Snap(int SnappingClient) {}

	virtual void PostSnap() {}

	/*
		Function: networkclipped(int snapping_client)
			Performs a series of test to see if a client can see the
			entity.

		Arguments:
			SnappingClient - ID of the client which snapshot is
				being generated. Could be -1 to create a complete
				snapshot of everything in the game for demo
				recording.

		Returns:
			Non-zero if the entity doesn't have to be in the snapshot.
	*/
	bool NetworkClipped(int SnappingClient, bool CheckShowAll = false, bool DefaultRange = false);
	bool NetworkClipped(int SnappingClient, vec2 CheckPos, bool CheckShowAll = false, bool DefaultRange = false);
	bool NetworkClippedLine(int SnappingClient, vec2 StartPos, vec2 EndPos, bool CheckShowAll = false);

	bool GameLayerClipped(vec2 CheckPos);

	// F-DDrace

	bool GetNearestAirPos(vec2 Pos, vec2 ColPos, vec2* pOutPos);
	bool GetNearestAirPosPlayer(vec2 PlayerPos, vec2* OutPos);

	int m_Number;
	int m_Layer;

	int GetObjType() { return m_ObjType; };
	bool IsAdvancedEntity();
	void SetPos(vec2 Pos) { m_Pos = Pos; }

	// used for entities inside of plots, created by the draw editor. if not on a plot but still from the editor, its 0, if not an object from editor its -1
	int m_PlotID;
	bool IsPlotDoor();

	// character drawing right now, only he can see the preview
	int m_BrushCID;
	// character transforming this entity right now (it being cut out and moved somewhere else) or when trying to save object
	int m_TransformCID;

	// initially wanted collision state
	bool m_InitialCollision;
	// whether other entities are affected by us, not guaranteed to work, only if implemented into the entity
	bool m_Collision;
	virtual void ResetCollision(bool Remove = false) {}
};

bool NetworkClipped(const CGameContext *pGameServer, int SnappingClient, vec2 CheckPos, int PlotID = -1, bool CheckShowAll = false, bool DefaultRange = false);
bool NetworkClippedLine(const CGameContext *pGameServer, int SnappingClient, vec2 StartPos, vec2 EndPos, int PlotID = -1, bool CheckShowAll = false);

#endif
