#include "elevtile.h"
#include "tileblock.h"
#include "elv_io.h"
#include "cmap.h"
#include <iostream>
#include <algorithm>
#define _USE_MATH_DEFINES
#include <math.h>

// ==================================================================================

ElevData::ElevData()
{
	width = 0;
	height = 0;
	dmin = 0;
	dmax = 0;
	dres = 1.0;
}

ElevData::ElevData(const ElevData &edata)
	: width(edata.width)
	, height(edata.height)
	, data(edata.data)
	, dmin(edata.dmin)
	, dmax(edata.dmax)
	, dres(edata.dres)
{
}

ElevData &ElevData::operator=(const ElevData &edata)
{
	width = edata.width;
	height = edata.height;
	data = edata.data;
	dmin = edata.dmin;
	dmax = edata.dmax;
	dres = edata.dres;
	return *this;
}

double ElevData::nodeValue(int ix, int iy) const
{
	return data[(ix+1) + (iy+1)*width];
}

void ElevData::setNodeValue(int ix, int iy, double v)
{
	data[(ix+1) + (iy+1)*width] = v;
}

ElevData ElevData::SubTile(const std::pair<DWORD, DWORD> &xrange, const std::pair<DWORD, DWORD> &yrange)
{
	ElevData sub;
	sub.width = xrange.second - xrange.first + 3;
	sub.height = yrange.second - yrange.first + 3;

	sub.data.resize(sub.width * sub.height);

	double *dataptr = data.data();
	double *subptr = sub.data.data();
	int y0 = TILE_FILERES - yrange.second;

	for (int y = 0; y < sub.height; y++) {
		memcpy(subptr + y*sub.width, dataptr + (y + y0) * width + xrange.first, sub.width * sizeof(double));
	}

	sub.dmin = *std::min_element(sub.data.begin(), sub.data.end());
	sub.dmax = *std::max_element(sub.data.begin(), sub.data.end());
	sub.dres = dres;

	return sub;
}

// ==================================================================================

const ZTreeMgr *ElevTile::s_treeMgr = 0;
const ZTreeMgr *ElevTile::s_treeModMgr = 0;

ElevTile::ElevTile(int lvl, int ilat, int ilng, ElevDisplayParam &elevDisplayParam)
	: Tile(lvl, ilat, ilng)
	, m_elevDisplayParam(elevDisplayParam)
{
	lat_subrange.second = 256;
	lng_subrange.second = 256;
	m_modified = false;
}

ElevTile::ElevTile(const ElevTile &etile)
	: Tile(etile)
	, m_elevDisplayParam(etile.m_elevDisplayParam)
	, m_edata(etile.m_edata)
{
	m_modified = false;
}

double ElevTile::nodeElevation(int ndx, int ndy)
{
	int idx = (ndy + 1)*m_edata.width + (ndx + 1);
	return (double)m_edata.data[idx];
}

bool ElevTile::Load(bool allowAncestorSubset)
{
	LoadData(m_edataBase, m_lvl, m_ilat, m_ilng);
	m_edata = m_edataBase;

	if (m_edata.data.size()) {
		LoadModData(m_edata, m_lvl, m_ilat, m_ilng);
	}
	else if (allowAncestorSubset) {
		// interpolate from ancestor
		LoadSubset();
	}

	// turn elevation data into an image
	if (m_edata.data.size())
		ExtractImage();

	return m_edata.data.size() > 0;
}

bool ElevTile::InterpolateFromAncestor()
{
	if (m_lvl <= 4) return false;
	int parent_lvl = m_lvl - 1;
	int parent_ilat = m_ilat / 2;
	int parent_ilng = m_ilng / 2;
	ElevTile parent(parent_lvl, parent_ilat, parent_ilng, m_elevDisplayParam);

	if (!parent.Load(false))
		if (!parent.InterpolateFromAncestor())
			return false;

	ElevTileBlock tblock = parent.Prolong();
	m_edata.width = TILE_ELEVSTRIDE;
	m_edata.height = TILE_ELEVSTRIDE;
	m_edata.data.resize(m_edata.width * m_edata.height);
	return tblock.getTile(m_ilat, m_ilng, this);
}

void ElevTile::LoadData(ElevData &edata, int lvl, int ilat, int ilng)
{
	if (s_openMode & 0x1) { // try cache
		char path[1024];
		sprintf(path, "%s/%s/%02d/%06d/%06d.elv", s_root.c_str(), Layer().c_str(), m_lvl, m_ilat, m_ilng);
		edata = elvread(path);
	}
	if (edata.data.size() == 0 && s_openMode & 0x2 && s_treeMgr) { // try archive
		BYTE *buf;
		DWORD ndata = s_treeMgr->ReadData(lvl, ilat, ilng, &buf);
		if (ndata) {
			edata = elvscan(buf, ndata);
			s_treeMgr->ReleaseData(buf);
		}
	}
}

void ElevTile::LoadModData(ElevData &edata, int lvl, int ilat, int ilng)
{
	bool found = false;
	if (s_openMode & 0x1) { // try cache
		char path[1024];
		sprintf(path, "%s/%s_mod/%02d/%06d/%06d.elv", s_root.c_str(), Layer().c_str(), m_lvl, m_ilat, m_ilng);
		found = elvmodread(path, edata);
	}
	if (!found && s_openMode & 0x2 && s_treeModMgr) { // try archive
		BYTE *buf;
		DWORD ndata = s_treeModMgr->ReadData(lvl, ilat, ilng, &buf);
		if (ndata) {
			elvmodscan(buf, ndata, edata);
			s_treeModMgr->ReleaseData(buf);
		}
	}
}

void ElevTile::LoadSubset()
{
	if (m_sublvl > 1) {
		lat_subrange.first /= 2;
		lat_subrange.second /= 2;
		lng_subrange.first /= 2;
		lng_subrange.second /= 2;
		if (m_subilat & 1) {
			lat_subrange.first += 128;
			lat_subrange.second += 128;
		}
		if (m_subilng & 1) {
			lng_subrange.first += 128;
			lng_subrange.second += 128;
		}
		m_sublvl -= 1;
		m_subilat /= 2;
		m_subilng /= 2;

		LoadData(m_edata, m_sublvl, m_subilat, m_subilng);
		if (m_edata.data.size()) {
			LoadModData(m_edata, m_sublvl, m_subilat, m_subilng);
			m_edata = m_edata.SubTile(lng_subrange, lat_subrange);
		}
		else {
			LoadSubset();
		}
	}
}

void ElevTile::Save()
{
	if (m_modified) {
		if (m_lvl == m_sublvl) { // for now, don't support saving ancestor subtiles
			char path[1024];
			sprintf(path, "%s/%s/%02d/%06d/%06d.elv", s_root.c_str(), Layer().c_str(), m_lvl, m_ilat, m_ilng);
			int nlat = (m_lvl < 4 ? 1 : 1 << (m_lvl - 4));
			int nlng = (m_lvl < 4 ? 1 : 1 << (m_lvl - 3));
			double latmax = (1.0 - (double)m_ilat / (double)nlat) * M_PI - 0.5*M_PI;
			double latmin = latmax - M_PI / nlat;
			double lngmin = (double)m_ilng / (double)nlng * 2.0*M_PI - M_PI;
			double lngmax = lngmin + 2.0*M_PI / nlng;

			ensureLayerDir(s_root.c_str(), Layer().c_str(), m_lvl, m_ilat);
			elvwrite(path, m_edata, latmin, latmax, lngmin, lngmax);
		}
		m_modified = false;
	}
}

void ElevTile::SaveMod()
{
	if (m_modified) {
		if (m_lvl == m_sublvl) { // for now, don't support saving ancestor subtiles
			char path[1024];
			sprintf(path, "%s_mod", Layer().c_str());
			ensureLayerDir(s_root.c_str(), path, m_lvl, m_ilat);
			sprintf(path, "%s/%s_mod/%02d/%06d/%06d.elv", s_root.c_str(), Layer().c_str(), m_lvl, m_ilat, m_ilng);
			int nlat = (m_lvl < 4 ? 1 : 1 << (m_lvl - 4));
			int nlng = (m_lvl < 4 ? 1 : 1 << (m_lvl - 3));
			double latmax = (1.0 - (double)m_ilat / (double)nlat) * M_PI - 0.5*M_PI;
			double latmin = latmax - M_PI / nlat;
			double lngmin = (double)m_ilng / (double)nlng * 2.0*M_PI - M_PI;
			double lngmax = lngmin + 2.0*M_PI / nlng;

			elvmodwrite(path, m_edata, m_edataBase, latmin, latmax, lngmin, lngmax);
		}
		m_modified = false;
	}
}

void ElevTile::displayParamChanged()
{
	ExtractImage();
}

void ElevTile::MatchNeighbourTiles()
{
	const double eps = 1e-6;

	int i, xblock, yblock, block_x0, block_y0, ilat, ilng, ilngn;

	int nlat = nLat();
	int nlng = nLng();

	std::vector<ElevTile*> tileGrid(3 * 3);
	for (int i = 0; i < tileGrid.size(); i++)
		tileGrid[i] = 0;

	// load the 3x3 tile neighbourhood
	ElevTileBlock etile3(m_lvl, m_ilat - 1, m_ilat + 2, m_ilng - 1, m_ilng + 2);

	for (yblock = 0; yblock < 3; yblock++) {
		ilat = m_ilat - yblock + 1;
		if (ilat < 0 || ilat >= nlat)
			continue;
		for (xblock = 0; xblock < 3; xblock++) {
			ilng = m_ilng + xblock - 1;
			if (ilat == m_ilat && ilng == m_ilng)
				continue;
			ilngn = (ilng < 0 ? ilng + nlng : ilng >= nlng ? ilng - nlng : ilng);
			ElevTile *etile = ElevTile::Load(m_lvl, ilat, ilngn, m_elevDisplayParam);
			if (!etile)
				continue;
			tileGrid[xblock + yblock * 3] = etile;
			etile3.setTile(ilat, ilng, etile);
		}
	}

	// place the modified central tile
	etile3.setTile(m_ilat, m_ilng, this);

	// check for modifications in the neighbours
	for (yblock = 0; yblock < 3; yblock++) {
		for (xblock = 0; xblock < 3; xblock++) {
			ElevTile *etile = tileGrid[xblock + yblock * 3];
			if (etile) {
				ilat = m_ilat - yblock + 1;
				ilng = m_ilng + xblock - 1;
				ilngn = (ilng < 0 ? ilng + nlng : ilng >= nlng ? ilng - nlng : ilng);
				ElevData edata = etile->getData();
				etile3.getTile(ilat, ilng, etile);
				for (i = 0; i < edata.data.size(); i++) {
					if (fabs(edata.data[i] - etile->getData().data[i]) > eps) {
						etile->dataChanged();
						etile->SaveMod();
						break;
					}
				}
				delete etile;
			}
		}
	}
}

bool ElevTile::MatchParentTile(int minlvl) const
{
	const double eps = 1e-6;

	if (m_lvl <= 4 || m_lvl <= minlvl)
		return false;

	int lvl = m_lvl - 1;
	int ilat = m_ilat / 2;
	int ilng = m_ilng / 2;

	ElevTile *etile = ElevTile::Load(lvl, ilat, ilng, m_elevDisplayParam);
	if (!etile)
		return false;

	ElevData &edata = etile->getData();
	ElevTileBlock *etile4 = ElevTileBlock::Load(m_lvl, ilat * 2 - 1, ilat * 2 + 3, ilng * 2 - 1, ilng * 2 + 3, m_elevDisplayParam);
	ElevData &edata4 = etile4->getData();

	int w4 = edata4.width;
	int h4 = edata4.height;
	int xofs = TILE_FILERES - 1;
	int yofs = TILE_FILERES - 1;
	int ofs = xofs + yofs * w4;
	bool isModified = false;

	for (int y = 0; y < edata.height; y++) {
		for (int x = 0; x < edata.width; x++) {
			int ref = ofs + y * 2 * w4 + x * 2;
			double v = (edata4.data[ref] * 4.0 +
				(edata4.data[ref - 1] + edata4.data[ref + 1] + edata4.data[ref - w4] + edata4.data[ref + w4]) * 2.0 +
				(edata4.data[ref - w4 - 1] + edata4.data[ref - w4 + 1] + edata4.data[ref + w4 - 1] + edata4.data[ref + w4 + 1])) / 16.0;
			if (fabs(edata.data[x + y*edata.width] - v) > eps) {
				edata.data[x + y*edata.width] = v;
				isModified = true;
			}
		}
	}
	delete etile4;

	if (isModified) {
		etile->dataChanged();
		etile->SaveMod();
		etile->MatchParentTile(minlvl); // recursively propagate changes down the quadtree
	}
	delete etile;

	return isModified;
}

ElevTileBlock ElevTile::Prolong()
{
	int i, j, ip, jp, idx;
	int ilat0 = m_ilat * 2;
	int ilat1 = ilat0 + 2;
	int ilng0 = m_ilng * 2;
	int ilng1 = ilng0 + 2;
	int lvl = m_lvl + 2;
	ElevTileBlock tblock(lvl, ilat0, ilat1, ilng0, ilng1);
	ElevData &edata = tblock.getData();

	for (i = 0; i < edata.height; i++) {
		ip = i - 1;
		for (j = 0; j < edata.width; j++) {
			idx = i*edata.width + j;
			jp = j - 1;
			if (!(ip & 1)) {
				if (!(jp & 1)) {
					edata.data[idx] = m_edata.nodeValue(jp / 2, ip / 2);
				}
				else {
					edata.data[idx] = (m_edata.nodeValue((jp - 1) / 2, ip / 2) +
						               m_edata.nodeValue((jp + 1) / 2, ip / 2)) * 0.5;
				}
			}
			else {
				if (!(jp & 1)) {
					edata.data[idx] = (m_edata.nodeValue(jp / 2, (ip - 1) / 2) +
						               m_edata.nodeValue(jp / 2, (ip + 1) / 2)) * 0.5;
				}
				else {
					edata.data[idx] = (m_edata.nodeValue((jp - 1) / 2, (ip - 1) / 2) +
						               m_edata.nodeValue((jp + 1) / 2, (ip - 1) / 2) +
						               m_edata.nodeValue((jp - 1) / 2, (ip + 1) / 2) +
						               m_edata.nodeValue((jp + 1) / 2, (ip + 1) / 2)) * 0.25;
				}
			}
		}
	}
	
	return tblock;
}

void ElevTile::setWaterMask(const MaskTile *mtile)
{
	int w = (m_edata.width - 2) * 2 - 2;
	int h = (m_edata.height - 2) * 2 - 2;

	const Image &mask = mtile->getImage();
	if (mask.width == w && mask.height == h) {
		m_waterMask.resize(w*h);
		for (int i = 0; i < h; i++) {
			for (int j = 0; j < w; j++) {
				m_waterMask[i*w + j] = ((mask.data[i*w + j] & 0xFF000000) == 0);
			}
		}
	}
}

ElevTile *ElevTile::Load(int lvl, int ilat, int ilng, ElevDisplayParam &elevDisplayParam, const Cmap *cm)
{
	ElevTile *etile = new ElevTile(lvl, ilat, ilng, elevDisplayParam);
	if (!etile->Load()) {
		delete etile;
		etile = 0;
	}
	return etile;
}

ElevTile *ElevTile::InterpolateFromAncestor(int lvl, int ilat, int ilng, ElevDisplayParam &elevDisplayParam, const Cmap *cm)
{
	ElevTile *etile = new ElevTile(lvl, ilat, ilng, elevDisplayParam);
	if (!etile->InterpolateFromAncestor()) {
		delete etile;
		etile = 0;
	}
	return etile;
}

void ElevTile::setTreeMgr(const ZTreeMgr *treeMgr, const ZTreeMgr *treeModMgr)
{
	s_treeMgr = treeMgr;
	s_treeModMgr = treeModMgr;
}

void ElevTile::dataChanged(int exmin, int exmax, int eymin, int eymax)
{
	ExtractImage(exmin, exmax, eymin, eymax);
	m_modified = true;
}

void ElevTile::ExtractImage(int exmin, int exmax, int eymin, int eymax)
{
	double dmin, dmax;

	img.width = (m_edata.width - 2) * 2 - 2;
	img.height = (m_edata.height - 2) * 2 - 2;
	img.data.resize(img.width * img.height);

	if (m_elevDisplayParam.autoRange) {
		dmin = m_edata.dmin;
		dmax = m_edata.dmax;
	}
	else {
		dmin = m_elevDisplayParam.rangeMin;
		dmax = m_elevDisplayParam.rangeMax;
	}
	double dscale = (dmax > dmin ? 256.0 / (dmax - dmin) : 1.0);

	int imin = (exmin < 0 ? 0 : max(0, (exmin - 1) * 2 - 1));
	int imax = (exmax < 0 ? img.width : min((int)img.width, exmax * 2));
	int jmin = (eymax < 0 ? 0 : max(0, (int)img.height - (eymax - 1) * 2));
	int jmax = (eymin < 0 ? img.height : min((int)img.height, (int)img.height - (eymin - 1) * 2 + 1));

	const Cmap &cm = cmap(m_elevDisplayParam.cmName);
	bool useMask = m_elevDisplayParam.useWaterMask && m_waterMask.size();

	for (int j = jmin; j < jmax; j++)
		for (int i = imin; i < imax; i++) {
			int ex = (i + 1) / 2 + 1;
			int ey = (img.height - j) / 2 + 1;
			double d = m_edata.data[ex + ey * m_edata.width];
			int v = max(min((int)((d - dmin) * dscale), 255), 0);
			if (v < 0 || v > 255)
				std::cerr << "Problem" << std::endl;
			img.data[i + j * img.width] = (0xff000000 | cm[v]);

			if (useMask && m_waterMask[i + j * img.width])
				img.data[i + j * img.width] = 0xffB9E3FF;
		}
}