#ifndef CMAP_H
#define CMAP_H

#include <windows.h>

enum CmapName {
	CMAP_GREY,
	CMAP_TOPO
};

typedef DWORD Cmap[256];

const Cmap &cmap(CmapName name);

#endif // !CMAP_H
