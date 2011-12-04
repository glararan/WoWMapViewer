#ifndef WMO_H
#define WMO_H

#include "manager.h"
#include "vec3d.h"
#include "mpq.h"
#include "model.h"
#include <vector>
#include <set>
#include "video.h"

class WMO;
class WMOGroup;
class WMOInstance;
class WMOManager;
class Liquid;


class WMOGroup {
	WMO *wmo;
	int flags;
	Vec3D v1,v2;
	int nTriangles, nVertices;
	GLuint dl,dl_light;
	Vec3D center;
	float rad;
	int num;
	int fog;
	int nDoodads;
	short *ddr;
	Liquid *lq;
public:
	Vec3D b1,b2;
	Vec3D vmin, vmax;
	bool indoor, hascv;
	bool visible;

	bool outdoorLights;
	std::string name;

	WMOGroup() : dl(0) {}
	~WMOGroup();
	void init(WMO *wmo, MPQFile &f, int num, char *names);
	void initDisplayList();
	void initLighting(int nLR, short *useLights);
	void draw(const Vec3D& ofs, const float rot);
	void drawLiquid();
	void drawDoodads(int doodadset, const Vec3D& ofs, const float rot);
	void setupFog();
};

struct WMOMaterial {
	int flags;
	int d1;
	int transparent;
	int nameStart;
	unsigned int col1;
	int d3;
	int nameEnd;
	unsigned int col2;
	int d4;
	float f1,f2;
	int dx[5];
	// read up to here -_-
	TextureID tex;
};

struct WMOLight {
	unsigned int flags, color;
	Vec3D pos;
	float intensity;
	float unk[5];
	float r;

	Vec4D fcolor;

	void init(MPQFile &f);
	void setup(GLint light);

	static void setupOnce(GLint light, Vec3D dir, Vec3D lcol);
};

struct WMOPV {
	Vec3D a,b,c,d;
};

struct WMOPR {
	short portal, group, dir, reserved;
};

struct WMODoodadSet {
	char name[0x14];
	int start;
	int size;
	int unused;
};

struct WMOLiquidHeader {
	int X, Y, A, B;
	Vec3D pos;
	short type;
};

struct WMOFog {
	unsigned int flags;
	Vec3D pos;
	float r1, r2, fogend, fogstart;
	unsigned int color1;
	float f2;
	float f3;
	unsigned int color2;
	// read to here (0x30 bytes)
	Vec4D color;
	void init(MPQFile &f);
	void setup();
};

class WMO: public ManagedItem {
public:
	WMOGroup *groups;
	int nTextures, nGroups, nP, nLights, nModels, nDoodads, nDoodadSets, nX;
	WMOMaterial *mat;
	Vec3D v1,v2;
	bool ok;
	std::vector<std::string> textures;
	std::vector<std::string> models;
	std::vector<ModelInstance> modelis;

	std::vector<WMOLight> lights;
	std::vector<WMOPV> pvs;
	std::vector<WMOPR> prs;

	std::vector<WMOFog> fogs;

	std::vector<WMODoodadSet> doodadsets;

	Model *skybox;
	int sbid;

	WMO(std::string name);
	~WMO();
	void draw(int doodadset, const Vec3D& ofs, const float rot);
	//void drawPortals();
	void drawSkybox();
};


class WMOManager: public SimpleManager {
public:
	int add(std::string name);
};


class WMOInstance {
	static std::set<int> ids;
public:
	WMO *wmo;
	Vec3D pos;
	Vec3D pos2, pos3, dir;
	int id, d2, d3;
	int doodadset;

	WMOInstance(WMO *wmo, MPQFile &f);
	void draw();
	//void drawPortals();

	static void reset();
};


#endif
