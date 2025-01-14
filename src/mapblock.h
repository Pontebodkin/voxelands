/************************************************************************
* Minetest-c55
* Copyright (C) 2010-2011 celeron55, Perttu Ahola <celeron55@gmail.com>
*
* mapblock.h
* voxelands - 3d voxel world sandbox game
* Copyright (C) Lisa 'darkrose' Milne 2013-2014 <lisa@ltmnet.com>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>
*
* License updated from GPLv2 or later to GPLv3 or later by Lisa Milne
* for Voxelands.
************************************************************************/


#ifndef MAPBLOCK_HEADER
#define MAPBLOCK_HEADER

#include <jmutex.h>
#include <jmutexautolock.h>
#include <exception>
#include "debug.h"
#include "common_irrlicht.h"
#include "mapnode.h"
#include "exceptions.h"
#include "serialization.h"
#include "constants.h"
#include "voxel.h"
#include "nodemetadata.h"
#include "staticobject.h"
#ifndef SERVER
	#include "mapblock_mesh.h"
#endif

class Map;

#define BLOCK_TIMESTAMP_UNDEFINED 0xffffffff

/*// Named by looking towards z+
enum{
	FACE_BACK=0,
	FACE_TOP,
	FACE_RIGHT,
	FACE_FRONT,
	FACE_BOTTOM,
	FACE_LEFT
};*/

enum ModifiedState
{
	// Has not been modified.
	MOD_STATE_CLEAN = 0,
	MOD_RESERVED1 = 1,
	// Has been modified, and will be saved when being unloaded.
	MOD_STATE_WRITE_AT_UNLOAD = 2,
	MOD_RESERVED3 = 3,
	// Has been modified, and will be saved as soon as possible.
	MOD_STATE_WRITE_NEEDED = 4,
	MOD_RESERVED5 = 5,
};

// NOTE: If this is enabled, set MapBlock to be initialized with
//       CONTENT_IGNORE.
/*enum BlockGenerationStatus
{
	// Completely non-generated (filled with CONTENT_IGNORE).
	BLOCKGEN_UNTOUCHED=0,
	// Trees or similar might have been blitted from other blocks to here.
	// Otherwise, the block contains CONTENT_IGNORE
	BLOCKGEN_FROM_NEIGHBORS=2,
	// Has been generated, but some neighbors might put some stuff in here
	// when they are generated.
	// Does not contain any CONTENT_IGNORE
	BLOCKGEN_SELF_GENERATED=4,
	// The block and all its neighbors have been generated
	BLOCKGEN_FULLY_GENERATED=6
};*/

#if 0
enum
{
	NODECONTAINER_ID_MAPBLOCK,
	NODECONTAINER_ID_MAPSECTOR,
	NODECONTAINER_ID_MAP,
	NODECONTAINER_ID_MAPBLOCKCACHE,
	NODECONTAINER_ID_VOXELMANIPULATOR,
};

class NodeContainer
{
public:
	virtual bool isValidPosition(v3s16 p) = 0;
	virtual MapNode getNode(v3s16 p) = 0;
	virtual void setNode(v3s16 p, MapNode & n) = 0;
	virtual u16 nodeContainerId() const = 0;

	MapNode getNodeNoEx(v3s16 p)
	{
		try{
			return getNode(p);
		}
		catch(InvalidPositionException &e){
			return MapNode(CONTENT_IGNORE);
		}
	}
};
#endif

/*
	MapBlock itself
*/

class MapBlock /*: public NodeContainer*/
{
public:
	MapBlock(Map *parent, v3s16 pos, bool dummy=false);
	~MapBlock();

	/*virtual u16 nodeContainerId() const
	{
		return NODECONTAINER_ID_MAPBLOCK;
	}*/

	Map * getParent()
	{
		return m_parent;
	}

	void reallocate()
	{
		if(data != NULL)
			delete[] data;
		u32 l = MAP_BLOCKSIZE * MAP_BLOCKSIZE * MAP_BLOCKSIZE;
		data = new MapNode[l];
		for(u32 i=0; i<l; i++){
			//data[i] = MapNode();
			data[i] = MapNode(CONTENT_IGNORE);
		}
		raiseModified(MOD_STATE_WRITE_NEEDED);
	}

	/*
		Flags
	*/

	bool isDummy()
	{
		return (data == NULL);
	}
	void unDummify()
	{
		assert(isDummy());
		reallocate();
	}

	/*
		This is called internally or externally after the block is
		modified, so that the block is saved and possibly not deleted from
		memory.
	*/
	// DEPRECATED, use *Modified()
	void setChangedFlag()
	{
		//dstream<<"Deprecated setChangedFlag() called"<<std::endl;
		raiseModified(MOD_STATE_WRITE_NEEDED);
	}
	// DEPRECATED, use *Modified()
	void resetChangedFlag()
	{
		//dstream<<"Deprecated resetChangedFlag() called"<<std::endl;
		resetModified();
	}
	// DEPRECATED, use *Modified()
	bool getChangedFlag()
	{
		//dstream<<"Deprecated getChangedFlag() called"<<std::endl;
		if(getModified() == MOD_STATE_CLEAN)
			return false;
		else
			return true;
	}

	// m_modified methods
	void raiseModified(u32 mod)
	{
		m_modified = MYMAX(m_modified, mod);
	}
	u32 getModified()
	{
		return m_modified;
	}
	void resetModified()
	{
		m_modified = MOD_STATE_CLEAN;
	}

	// is_underground getter/setter
	bool getIsUnderground()
	{
		return is_underground;
	}
	void setIsUnderground(bool a_is_underground)
	{
		is_underground = a_is_underground;
		raiseModified(MOD_STATE_WRITE_NEEDED);
	}

#ifndef SERVER
	void setMeshExpired(bool expired)
	{
		m_mesh_expired = expired;
	}

	bool getMeshExpired()
	{
		return m_mesh_expired;
	}
#endif

	void setLightingExpired(bool expired)
	{
		if(expired != m_lighting_expired){
			m_lighting_expired = expired;
			raiseModified(MOD_STATE_WRITE_NEEDED);
		}
	}
	bool getLightingExpired()
	{
		return m_lighting_expired;
	}

	bool isGenerated()
	{
		return m_generated;
	}
	void setGenerated(bool b)
	{
		if(b != m_generated){
			raiseModified(MOD_STATE_WRITE_NEEDED);
			m_generated = b;
		}
	}

	bool isValid()
	{
		if(m_lighting_expired)
			return false;
		if(data == NULL)
			return false;
		return true;
	}

	/*
		Position stuff
	*/

	v3s16 getPos()
	{
		return m_pos;
	}

	v3s16 getPosRelative()
	{
		return m_pos * MAP_BLOCKSIZE;
	}

	uint8_t getBiome()
	{
		return m_biome;
	}

	void setBiome(uint8_t biome)
	{
		m_biome = biome;
	}

	core::aabbox3d<s16> getBox()
	{
		return core::aabbox3d<s16>(getPosRelative(),
				getPosRelative()
				+ v3s16(MAP_BLOCKSIZE, MAP_BLOCKSIZE, MAP_BLOCKSIZE)
				- v3s16(1,1,1));
	}

	/*
		Regular MapNode get-setters
	*/

	bool isValidPosition(s16 x, s16 y, s16 z)
	{
		if (data == NULL)
			return false;
		return (
			x >= 0 && x < MAP_BLOCKSIZE
			&& y >= 0 && y < MAP_BLOCKSIZE
			&& z >= 0 && z < MAP_BLOCKSIZE
		);
	}

	MapNode getNode(s16 x, s16 y, s16 z, bool *valid_position)
	{
		*valid_position = isValidPosition(x, y, z);
		if (!*valid_position)
			return MapNode(CONTENT_IGNORE);
		return data[z*MAP_BLOCKSIZE*MAP_BLOCKSIZE + y*MAP_BLOCKSIZE + x];
	}

	MapNode getNode(v3s16 p, bool *valid_position)
	{
		return getNode(p.X, p.Y, p.Z, valid_position);
	}

	MapNode getNodeNoEx(v3s16 p)
	{
		bool is_valid;
		MapNode node = getNode(p.X, p.Y, p.Z, &is_valid);
		if (!is_valid)
			return MapNode(CONTENT_IGNORE);
		return node;
	}

	void setNode(s16 x, s16 y, s16 z, MapNode & n)
	{
		if (!isValidPosition(x,y,z))
			throw InvalidPositionException();
		data[z*MAP_BLOCKSIZE*MAP_BLOCKSIZE + y*MAP_BLOCKSIZE + x] = n;
		raiseModified(MOD_STATE_WRITE_NEEDED);
	}

	void setNode(v3s16 p, MapNode & n)
	{
		setNode(p.X, p.Y, p.Z, n);
	}

	void incNodeTicks(v3s16 p)
	{
		if (!isValidPosition(p.X,p.Y,p.Z))
			return;
		data[p.Z*MAP_BLOCKSIZE*MAP_BLOCKSIZE + p.Y*MAP_BLOCKSIZE + p.X].envticks++;
	}

	/*
		Non-checking variants of the above
	*/

	MapNode getNodeNoCheck(s16 x, s16 y, s16 z, bool *valid_position)
	{
		*valid_position = isValidPosition(x, y, z);
		if (!*valid_position)
			return MapNode(CONTENT_IGNORE);
		return data[z*MAP_BLOCKSIZE*MAP_BLOCKSIZE + y*MAP_BLOCKSIZE + x];
	}

	MapNode getNodeNoCheck(v3s16 p, bool *valid_position)
	{
		return getNodeNoCheck(p.X, p.Y, p.Z, valid_position);
	}

	void setNodeNoCheck(s16 x, s16 y, s16 z, MapNode & n)
	{
		if(data == NULL)
			throw InvalidPositionException();
		data[z*MAP_BLOCKSIZE*MAP_BLOCKSIZE + y*MAP_BLOCKSIZE + x] = n;
		raiseModified(MOD_STATE_WRITE_NEEDED);
	}

	void setNodeNoCheck(v3s16 p, MapNode & n)
	{
		setNodeNoCheck(p.X, p.Y, p.Z, n);
	}

	/*
		These functions consult the parent container if the position
		is not valid on this MapBlock.
	*/
	bool isValidPositionParent(v3s16 p);
	MapNode getNodeParent(v3s16 p, bool *is_valid_position = NULL);
	void setNodeParent(v3s16 p, MapNode & n);

	void drawbox(s16 x0, s16 y0, s16 z0, s16 w, s16 h, s16 d, MapNode node)
	{
		for(u16 z=0; z<d; z++)
			for(u16 y=0; y<h; y++)
				for(u16 x=0; x<w; x++)
					setNode(x0+x, y0+y, z0+z, node);
	}

	/*
		Graphics-related methods
	*/

	/*// A quick version with nodes passed as parameters
	u8 getFaceLight(u32 daynight_ratio, MapNode n, MapNode n2,
			v3s16 face_dir);*/
	/*// A more convenient version
	u8 getFaceLight(u32 daynight_ratio, v3s16 p, v3s16 face_dir)
	{
		return getFaceLight(daynight_ratio,
				getNodeParentNoEx(p),
				getNodeParentNoEx(p + face_dir),
				face_dir);
	}*/
	u8 getFaceLight2(u32 daynight_ratio, v3s16 p, v3s16 face_dir)
	{
		return getFaceLight(daynight_ratio,
				getNodeParent(p),
				getNodeParent(p + face_dir),
				face_dir);
	}

	// See comments in mapblock.cpp
	bool propagateSunlight(core::map<v3s16, bool> & light_sources,
			bool remove_light=false, bool *black_air_left=NULL);

	// Copies data to VoxelManipulator to getPosRelative()
	void copyTo(VoxelManipulator &dst);
	// Copies data from VoxelManipulator getPosRelative()
	void copyFrom(VoxelManipulator &dst);

	/*
		Update day-night lighting difference flag.

		Sets m_day_night_differs to appropriate value.

		These methods don't care about neighboring blocks.
		It means that to know if a block really doesn't need a mesh
		update between day and night, the neighboring blocks have
		to be taken into account. Use Map::dayNightDiffed().
	*/
	void updateDayNightDiff();

	bool dayNightDiffed()
	{
		return m_day_night_differs;
	}

	/*
		Miscellaneous stuff
	*/

	/*
		Tries to measure ground level.
		Return value:
			-1 = only air
			-2 = only ground
			-3 = random fail
			0...MAP_BLOCKSIZE-1 = ground level
	*/
	s16 getGroundLevel(v2s16 p2d);

	/*
		Timestamp (see m_timestamp)
		NOTE: BLOCK_TIMESTAMP_UNDEFINED=0xffffffff means there is no timestamp.
	*/
	void setTimestamp(u32 time)
	{
		m_timestamp = time;
		raiseModified(MOD_STATE_WRITE_AT_UNLOAD);
	}
	void setTimestampNoChangedFlag(u32 time)
	{
		m_timestamp = time;
	}
	u32 getTimestamp()
	{
		return m_timestamp;
	}

	/*
		See m_usage_timer
	*/
	void resetUsageTimer()
	{
		m_usage_timer = 0;
	}
	void incrementUsageTimer(float dtime)
	{
		m_usage_timer += dtime;
	}
	u32 getUsageTimer()
	{
		return m_usage_timer;
	}

	/*
		Serialization
	*/

	// These don't write or read version by itself
	void serialize(std::ostream &os, u8 version);
	void deSerialize(std::istream &is, u8 version);
	// Used after the basic ones when writing on disk (serverside)
	void serializeDiskExtra(std::ostream &os, u8 version);
	void deSerializeDiskExtra(std::istream &is, u8 version);

	// Used by the server env for mob spawning
	bool has_spawn_area;
	v3s16 spawn_area;
	u32 last_spawn;

private:
	/*
		Private methods
	*/

	/*
		Used only internally, because changes can't be tracked
	*/

	MapNode & getNodeRef(s16 x, s16 y, s16 z)
	{
		if(data == NULL)
			throw InvalidPositionException();
		if(x < 0 || x >= MAP_BLOCKSIZE) throw InvalidPositionException();
		if(y < 0 || y >= MAP_BLOCKSIZE) throw InvalidPositionException();
		if(z < 0 || z >= MAP_BLOCKSIZE) throw InvalidPositionException();
		return data[z*MAP_BLOCKSIZE*MAP_BLOCKSIZE + y*MAP_BLOCKSIZE + x];
	}
	MapNode & getNodeRef(v3s16 &p)
	{
		return getNodeRef(p.X, p.Y, p.Z);
	}

public:
	/*
		Public member variables
	*/

#ifndef SERVER // Only on client
	MapBlockMesh *mesh;
	JMutex mesh_mutex;

	std::map<v3s16,MapBlockSound> m_sounds;
#endif

	NodeMetadataList m_node_metadata;
	StaticObjectList m_static_objects;
	std::list<u16> m_active_objects;

private:
	/*
		Private member variables
	*/

	// NOTE: Lots of things rely on this being the Map
	Map *m_parent;
	// Position in blocks on parent
	v3s16 m_pos;

	uint8_t m_biome;

	/*
		If NULL, block is a dummy block.
		Dummy blocks are used for caching not-found-on-disk blocks.
	*/
	MapNode * data;

	/*
		- On the server, this is used for telling whether the
		  block has been modified from the one on disk.
		- On the client, this is used for nothing.
	*/
	u32 m_modified;

	/*
		When propagating sunlight and the above block doesn't exist,
		sunlight is assumed if this is false.

		In practice this is set to true if the block is completely
		undeground with nothing visible above the ground except
		caves.
	*/
	bool is_underground;

	/*
		Set to true if changes has been made that make the old lighting
		values wrong but the lighting hasn't been actually updated.

		If this is false, lighting is exactly right.
		If this is true, lighting might be wrong or right.
	*/
	bool m_lighting_expired;

	// Whether day and night lighting differs
	bool m_day_night_differs;

	bool m_generated;

#ifndef SERVER // Only on client
	/*
		Set to true if the mesh has been ordered to be updated
		sometime in the background.
		In practice this is set when the day/night lighting switches.
	*/
	bool m_mesh_expired;
#endif

	/*
		When block is removed from active blocks, this is set to gametime.
		Value BLOCK_TIMESTAMP_UNDEFINED=0xffffffff means there is no timestamp.
	*/
	u32 m_timestamp;

	/*
		When the block is accessed, this is set to 0.
		Map will unload the block when this reaches a timeout.
	*/
	float m_usage_timer;
};

inline bool blockpos_over_limit(v3s16 p)
{
	return
	  (p.X < -MAP_GENERATION_LIMIT / MAP_BLOCKSIZE
	|| p.X >  MAP_GENERATION_LIMIT / MAP_BLOCKSIZE
	|| p.Y < -MAP_GENERATION_LIMIT / MAP_BLOCKSIZE
	|| p.Y >  MAP_GENERATION_LIMIT / MAP_BLOCKSIZE
	|| p.Z < -MAP_GENERATION_LIMIT / MAP_BLOCKSIZE
	|| p.Z >  MAP_GENERATION_LIMIT / MAP_BLOCKSIZE);
}

/*
	Returns the position of the block where the node is located
*/
inline v3s16 getNodeBlockPos(v3s16 p)
{
	return getContainerPos(p, MAP_BLOCKSIZE);
}

inline v2s16 getNodeSectorPos(v2s16 p)
{
	return getContainerPos(p, MAP_BLOCKSIZE);
}

inline s16 getNodeBlockY(s16 y)
{
	return getContainerPos(y, MAP_BLOCKSIZE);
}

/*
	Get a quick string to describe what a block actually contains
*/
std::string analyze_block(MapBlock *block);

#endif

