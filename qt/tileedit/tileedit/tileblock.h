#ifndef TILEBLOCK_H
#define TILEBLOCK_H

#include "tile.h"
#include "elevtile.h"

class TileBlock
{
public:
	TileBlock(int lvl, int ilat0, int ilat1, int ilng0, int ilng1);
	TileBlock(const TileBlock &tileblock);
	virtual ~TileBlock();
	int Level() const { return m_lvl; }
	int iLat0() const { return m_ilat0; }
	int iLat1() const { return m_ilat1; }
	int iLng0() const { return m_ilng0; }
	int iLng1() const { return m_ilng1; }
	int nLat() const { return (m_lvl < 4 ? 1 : 1 << (m_lvl - 4)); }
	int nLng() const { return (m_lvl < 4 ? 1 : 1 << (m_lvl - 3)); }

	virtual bool setTile(int ilat, int ilng, const Tile *tile) { return false; }

	/**
	 * \brief Returns a pointer to one of the constituent
	 */
	const Tile *getTile(int ilat, int ilng) const;

	/**
	 * \brief Copies one of the constituent tiles and returns a pointer to the copy.
	 *
	 * The caller owns the copy and is responsible for deleting it.
	 */
	virtual Tile *copyTile(int ilat, int ilng) const = 0;

	/**
	 * \brief Copy one of the constituent tiles into an existing tile.
	 */
	virtual bool copyTile(int ilat, int ilng, Tile *tile) const = 0;

protected:
	Tile *getTile(int ilat, int ilng);

	int m_lvl;
	int m_ilat0, m_ilat1;
	int m_ilng0, m_ilng1;
	int m_nblocklat, m_nblocklng;

	std::vector<Tile*> m_tile; ///< constituent tiles
};


class SurfTileBlock : public TileBlock
{
public:
	static SurfTileBlock *Load(int lvl, int ilat0, int ilat1, int ilng0, int ilng1);
	virtual Tile *copyTile(int ilat, int ilng) const;
	virtual bool copyTile(int ilat, int ilng, Tile *tile) const;

protected:
	SurfTileBlock(int lvl, int ilat0, int ilat1, int ilng0, int ilng1);
};


class NightlightTileBlock : public TileBlock
{
	friend class MaskTileBlock;

public:
	virtual Tile *copyTile(int ilat, int ilng) const;
	virtual bool copyTile(int ilat, int ilng, Tile *tile) const;

protected:
	NightlightTileBlock(int lvl, int ilat0, int ilat1, int ilng0, int ilng1);
};


class MaskTileBlock : public TileBlock
{
public:
	static std::pair<MaskTileBlock*, NightlightTileBlock*> Load(int lvl, int ilat0, int ilat1, int ilng0, int ilng1);
	virtual Tile *copyTile(int ilat, int ilng) const;
	virtual bool copyTile(int ilat, int ilng, Tile *tile) const;

protected:
	MaskTileBlock(int lvl, int ilat0, int ilat1, int ilng0, int ilng1);
};


class ElevTileBlock : public TileBlock
{
public:
	static ElevTileBlock *Load(int lvl, int ilat0, int ilat1, int ilng0, int ilng1, ElevDisplayParam &elevDisplayParam);
	ElevTileBlock(int lvl, int ilat0, int ilat1, int ilng0, int ilng1);
	ElevTileBlock(const ElevTileBlock &etileblock);
	bool setTile(int ilat, int ilng, const Tile *tile);
	//bool getTile(int ilat, int ilng, Tile *tile) const;
	ElevData &getData() { return m_edata; }
	virtual Tile *copyTile(int ilat, int ilng) const;
	virtual bool copyTile(int ilat, int ilng, Tile *tile) const;

private:
	ElevData m_edata;
};

#endif // !TILEBLOCK_H