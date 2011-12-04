#include "maptile.h"
#include "world.h"
#include "vec3d.h"
#include <cassert>
#include <algorithm>
using namespace std;


MapTile::MapTile(int x0, int z0, char* filename): x(x0), z(z0), topnode(0,0,16)
{
	xbase = x0 * TILESIZE;
	zbase = z0 * TILESIZE;

	gLog("Loading tile %d,%d\n",x0,z0);

	MPQFile f(filename);
	ok = !f.isEof();
	if (!ok) {
		gLog("-> Error loading %s\n",filename);
		return;
	}

	char fourcc[5];
	size_t size;

	size_t mcnk_offsets[256], mcnk_sizes[256];

	while (!f.isEof()) {
		f.read(fourcc,4);
		f.read(&size, 4);

		flipcc(fourcc);
		fourcc[4] = 0;

		size_t nextpos = f.getPos() + size;

		if (!strcmp(fourcc,"MCIN")) {
			// mapchunk offsets/sizes
			for (int i=0; i<256; i++) {
				f.read(&mcnk_offsets[i],4);
				f.read(&mcnk_sizes[i],4);
				f.seekRelative(8);
			}
		}
		else if (!strcmp(fourcc,"MTEX")) {
			// texture lists
			char *buf = new char[size];
			f.read(buf, size);
			char *p=buf;
			int t=0;
			while (p<buf+size) {
				string texpath(p);
				p+=strlen(p)+1;
				fixname(texpath);
				video.textures.add(texpath);
				textures.push_back(texpath);
			}
			delete[] buf;
		}
		else if (!strcmp(fourcc,"MMDX")) {
			// models ...
			// MMID would be relative offsets for MMDX filenames
			if (size) {
				char *buf = new char[size];
				f.read(buf, size);
				char *p=buf;
				int t=0;
				while (p<buf+size) {
					string path(p);
					p+=strlen(p)+1;
					fixname(path);

					gWorld->modelmanager.add(path);
					models.push_back(path);
				}
				delete[] buf;
			}
		}
		else if (!strcmp(fourcc,"MWMO")) {
			// map objects
			// MWID would be relative offsets for MWMO filenames
			if (size) {
				char *buf = new char[size];
				f.read(buf, size);
				char *p=buf;
				while (p<buf+size) {
					string path(p);
					p+=strlen(p)+1;
					fixname(path);
					
					gWorld->wmomanager.add(path);
					wmos.push_back(path);
				}
				delete[] buf;
			}
		}
		else if (!strcmp(fourcc,"MDDF")) {
			// model instance data
			nMDX = (int)size / 36;
			for (int i=0; i<nMDX; i++) {
				int id;
				f.read(&id, 4);
				Model *model = (Model*)gWorld->modelmanager.items[gWorld->modelmanager.get(models[id])];
				ModelInstance inst(model, f);
				modelis.push_back(inst);
			}
		}
		else if (!strcmp(fourcc,"MODF")) {
			// wmo instance data
			nWMO = (int)size / 64;
			for (int i=0; i<nWMO; i++) {
				int id;
				f.read(&id, 4);
				WMO *wmo = (WMO*)gWorld->wmomanager.items[gWorld->wmomanager.get(wmos[id])];
				WMOInstance inst(wmo, f);
				wmois.push_back(inst);
			}
		}

		// MCNK data will be processed separately ^_^

		f.seek((int)nextpos);
	}

	// read individual map chunks
	for (int j=0; j<16; j++) {
		for (int i=0; i<16; i++) {
			f.seek((int)mcnk_offsets[j*16+i]);
			chunks[j][i].init(this, f);
		}
	}

	// init quadtree
	topnode.setup(this);

	f.close();
}

MapTile::~MapTile()
{
	if (!ok) return;

	gLog("Unloading tile %d,%d\n", x, z);

	topnode.cleanup();

	for (int j=0; j<16; j++) {
		for (int i=0; i<16; i++) {
			chunks[j][i].destroy();
		}
	}

	for (vector<string>::iterator it = textures.begin(); it != textures.end(); ++it) {
        video.textures.delbyname(*it);
	}

	for (vector<string>::iterator it = wmos.begin(); it != wmos.end(); ++it) {
		gWorld->wmomanager.delbyname(*it);
	}

	for (vector<string>::iterator it = models.begin(); it != models.end(); ++it) {
		gWorld->modelmanager.delbyname(*it);
	}
}

void MapTile::draw()
{
	if (!ok) return;

	
	for (int j=0; j<16; j++) {
		for (int i=0; i<16; i++) {
			chunks[j][i].visible = false;
			//chunks[j][i].draw();
		}
	}
	
	topnode.draw();

}

void MapTile::drawWater()
{
	if (!ok) return;

	for (int j=0; j<16; j++) {
		for (int i=0; i<16; i++) {
			if (chunks[j][i].visible) chunks[j][i].drawWater();
		}
	}
}

void MapTile::drawObjects()
{
	if (!ok) return;

	for (int i=0; i<nWMO; i++) {
		wmois[i].draw();
	}
}

void MapTile::drawSky()
{
	if (!ok) return;

	for (int i=0; i<nWMO; i++) {
		wmois[i].wmo->drawSkybox();
		if (gWorld->hadSky) break;
	}
}

/*
void MapTile::drawPortals()
{
	if (!ok) return;

	for (int i=0; i<nWMO; i++) {
		wmois[i].drawPortals();
	}
}
*/

void MapTile::drawModels()
{
	if (!ok) return;

	for (int i=0; i<nMDX; i++) {
		modelis[i].draw();
	}
}

int holetab_h[4] = {0x1111, 0x2222, 0x4444, 0x8888};
int holetab_v[4] = {0x000F, 0x00F0, 0x0F00, 0xF000};

bool isHole(int holes, int i, int j)
{
	return (holes & holetab_h[i] & holetab_v[j])!=0;
}


int indexMapBuf(int x, int y)
{
	return ((y+1)/2)*9 + (y/2)*8 + x;
}

struct MapChunkHeader {
	uint32 flags;
	uint32 ix;
	uint32 iy;
	uint32 nLayers;
	uint32 nDoodadRefs;
	uint32 ofsHeight;
	uint32 ofsNormal;
	uint32 ofsLayer;
	uint32 ofsRefs;
	uint32 ofsAlpha;
	uint32 sizeAlpha;
	uint32 ofsShadow;
	uint32 sizeShadow;
	uint32 areaid;
	uint32 nMapObjRefs;
	uint32 holes;
	uint16 s1;
	uint16 s2;
	uint32 d1;
	uint32 d2;
	uint32 d3;
	uint32 predTex;
	uint32 nEffectDoodad;
	uint32 ofsSndEmitters;
	uint32 nSndEmitters;
	uint32 ofsLiquid;
	uint32 sizeLiquid;
	float  zpos;
	float  xpos;
	float  ypos;
	uint32 textureId;
	uint32 props;
	uint32 effectId;
};

void MapChunk::init(MapTile* mt, MPQFile &f)
{
	Vec3D tn[mapbufsize], tv[mapbufsize];

    f.seekRelative(4);
	char fcc[5];
	size_t size;
	f.read(&size, 4);

	// okay here we go ^_^
	size_t lastpos = f.getPos() + size;

	//char header[0x80];
	MapChunkHeader header;
	f.read(&header, 0x80);

	areaID = header.areaid;
	
    zbase = header.zpos;
    xbase = header.xpos;
    ybase = header.ypos;

	int holes = header.holes;
	int chunkflags = header.flags;

	hasholes = (holes != 0);

	/*
	if (hasholes) {
		gLog("Holes: %d\n", holes);
		int k=1;
		for (int j=0; j<4; j++) {
			for (int i=0; i<4; i++) {
				gLog((holes & k)?"1":"0");
				k <<= 1;
			}
			gLog("\n");
		}
	}
	*/

	// correct the x and z values ^_^
	zbase = zbase*-1.0f + ZEROPOINT;
	xbase = xbase*-1.0f + ZEROPOINT;

	vmin = Vec3D( 9999999.0f, 9999999.0f, 9999999.0f);
	vmax = Vec3D(-9999999.0f,-9999999.0f,-9999999.0f);
	
	while (f.getPos() < lastpos) {
		f.read(fcc,4);
		f.read(&size, 4);

		flipcc(fcc);
		fcc[4] = 0;

		size_t nextpos = f.getPos() + size;

		if (!strcmp(fcc,"MCNR")) {
			nextpos = f.getPos() + 0x1C0; // size fix
			// normal vectors
			char nor[3];
			Vec3D *ttn = tn;
			for (int j=0; j<17; j++) {
				for (int i=0; i<((j%2)?8:9); i++) {
					f.read(nor,3);
					// order Z,X,Y ?
					//*ttn++ = Vec3D((float)nor[0]/127.0f, (float)nor[2]/127.0f, (float)nor[1]/127.0f);
					*ttn++ = Vec3D(-(float)nor[1]/127.0f, (float)nor[2]/127.0f, -(float)nor[0]/127.0f);
				}
			}
		}
		else if (!strcmp(fcc,"MCVT")) {
			Vec3D *ttv = tv;

			// vertices
			for (int j=0; j<17; j++) {
				for (int i=0; i<((j%2)?8:9); i++) {
					float h,xpos,zpos;
					f.read(&h,4);
					xpos = i * UNITSIZE;
					zpos = j * 0.5f * UNITSIZE;
					if (j%2) {
                        xpos += UNITSIZE*0.5f;
					}
					Vec3D v = Vec3D(xbase+xpos, ybase+h, zbase+zpos);
					*ttv++ = v;
					if (v.y < vmin.y) vmin.y = v.y;
					if (v.y > vmax.y) vmax.y = v.y;
				}
			}

			vmin.x = xbase;
			vmin.z = zbase;
			vmax.x = xbase + 8 * UNITSIZE;
			vmax.z = zbase + 8 * UNITSIZE;
			r = (vmax - vmin).length() * 0.5f;

		}
		else if (!strcmp(fcc,"MCLY")) {
			// texture info
			nTextures = (int)size / 16;
			//gLog("=\n");
			for (int i=0; i<nTextures; i++) {
				int tex, flags;
				f.read(&tex,4);
				f.read(&flags, 4);

				f.seekRelative(8);

				flags &= ~0x100;

				if (flags & 0x80) {
                    animated[i] = flags;
				} else {
					animated[i] = 0;
				}

				/*
				if (flags != 0) {
					gLog("Texture layer flags: %x ", flags);
					int v = 0x80;
					for (int i=0; i<8; i++,v>>=1) {
						gLog("%c%s", (flags&v)?'1':'-', i==3?" ":"");
					}

					gLog(" %s\n", mt->textures[tex].c_str());
				}
				*/

				/*
				if (mt->textures[tex]=="Tileset\\BurningStepps\\BurningSteppsLavatest02.blp") {
					gLog("Lava tex:\t%d\t%d\t%d\n", unk[0], unk[1], unk[2]);
				} else {
					gLog("---- tex:\t%d\t%d\t%d\n", unk[0], unk[1], unk[2]);
				}
				*/

				textures[i] = video.textures.get(mt->textures[tex]);
			}
		}
		else if (!strcmp(fcc,"MCSH")) {
			// shadow map 64 x 64
			unsigned char sbuf[64*64], *p, c[8];
			p = sbuf;
			for (int j=0; j<64; j++) {
				f.read(c,8);
				for (int i=0; i<8; i++) {
					for (int b=0x01; b!=0x100; b<<=1) {
						*p++ = (c[i] & b) ? 85 : 0;
					}
				}
			}
			glGenTextures(1, &shadow);
			glBindTexture(GL_TEXTURE_2D, shadow);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 64, 64, 0, GL_ALPHA, GL_UNSIGNED_BYTE, sbuf);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		}
		else if (!strcmp(fcc,"MCAL")) {
			// alpha maps  64 x 64
			if (nTextures>0) {
				glGenTextures(nTextures-1, alphamaps);
				for (int i=0; i<nTextures-1; i++) {
					glBindTexture(GL_TEXTURE_2D, alphamaps[i]);
					unsigned char amap[64*64], *p;
					char *abuf = f.getPointer();
					p = amap;
					for (int j=0; j<64; j++) {
						for (int i=0; i<32; i++) {
							unsigned char c = *abuf++;
							*p++ = (c & 0x0f) << 4;
							*p++ = (c & 0xf0);
						}

					}
					glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 64, 64, 0, GL_ALPHA, GL_UNSIGNED_BYTE, amap);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
					f.seekRelative(0x800);
				}
			} else {
				// some MCAL chunks have incorrect sizes! :(
                continue;
			}
		}
		else if (!strcmp(fcc,"MCLQ")) {
			// liquid / water level
			char fcc1[5];
			f.read(fcc1,4);
			flipcc(fcc1);
			fcc1[4]=0;
			if (!strcmp(fcc1,"MCSE")) {
				haswater = false;
			}
			else {
				haswater = true;
				f.seekRelative(-4);
				f.read(&waterlevel,4);

				if (waterlevel > vmax.y) vmax.y = waterlevel;
				if (waterlevel < vmin.y) haswater = false;

				f.seekRelative(4);

				lq = new Liquid(8, 8, Vec3D(xbase, waterlevel, zbase));
				//lq->init(f);
				lq->initFromTerrain(f, chunkflags);

				/*
				// let's output some debug info! ( '-')b
				string lq = "";
				if (flags & 4) lq.append(" river");
				if (flags & 8) lq.append(" ocean");
				if (flags & 16) lq.append(" magma");
				if (flags & 32) lq.append(" slime?");
				gLog("LQ%s (base:%f)\n", lq.c_str(), waterlevel);
				*/

			}
			// we're done here!
			break;
		}
		f.seek((int)nextpos);
	}

	// create vertex buffers
	glGenBuffersARB(1,&vertices);
	glGenBuffersARB(1,&normals);

	glBindBufferARB(GL_ARRAY_BUFFER_ARB, vertices);
	glBufferDataARB(GL_ARRAY_BUFFER_ARB, mapbufsize*3*sizeof(float), tv, GL_STATIC_DRAW_ARB);

	glBindBufferARB(GL_ARRAY_BUFFER_ARB, normals);
	glBufferDataARB(GL_ARRAY_BUFFER_ARB, mapbufsize*3*sizeof(float), tn, GL_STATIC_DRAW_ARB);

	if (hasholes) initStrip(holes);
	/*
	else {
		strip = gWorld->mapstrip;
		striplen = 16*18 + 7*2 + 8*2; //stripsize;
	}
	*/

	this->mt = mt;

	vcenter = (vmin + vmax) * 0.5f;

}


void MapChunk::initStrip(int holes)
{
	strip = new short[256]; // TODO: figure out exact length of strip needed
	short *s = strip;
	bool first = true;
	for (int y=0; y<4; y++) {
		for (int x=0; x<4; x++) {
			if (!isHole(holes, x, y)) {
				// draw tile here
				// this is ugly but sort of works
				int i = x*2;
				int j = y*4;
				for (int k=0; k<2; k++) {
					if (!first) {
						*s++ = indexMapBuf(i,j+k*2);
					} else first = false;
					for (int l=0; l<3; l++) {
						*s++ = indexMapBuf(i+l,j+k*2);
						*s++ = indexMapBuf(i+l,j+k*2+2);
					}
					*s++ = indexMapBuf(i+2,j+k*2+2);
				}
			}
		}
	}
	striplen = (int)(s - strip);
}


void MapChunk::destroy()
{
	// unload alpha maps
	glDeleteTextures(nTextures-1, alphamaps);
	// shadow maps, too
	glDeleteTextures(1, &shadow);

	// delete VBOs
	glDeleteBuffersARB(1, &vertices);
	glDeleteBuffersARB(1, &normals);

	if (hasholes) delete[] strip;

	if (haswater) delete lq;
}

void MapChunk::drawPass(int anim)
{
	if (anim) {
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glMatrixMode(GL_TEXTURE);
		glPushMatrix();

		// note: this is ad hoc and probably completely wrong
		int spd = (anim & 0x08) | ((anim & 0x10) >> 2) | ((anim & 0x20) >> 4) | ((anim & 0x40) >> 6);
		int dir = anim & 0x07;
		const float texanimxtab[8] = {0, 1, 1, 1, 0, -1, -1, -1};
		const float texanimytab[8] = {1, 1, 0, -1, -1, -1, 0, 1};
		float fdx = -texanimxtab[dir], fdy = texanimytab[dir];

		int animspd = (int)(200.0f * detail_size);
		float f = ( ((int)(gWorld->animtime*(spd/15.0f))) % animspd) / (float)animspd;
		glTranslatef(f*fdx,f*fdy,0);
	}

	glDrawElements(GL_TRIANGLE_STRIP, striplen, GL_UNSIGNED_SHORT, strip);

	if (anim) {
        glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
		glActiveTextureARB(GL_TEXTURE1_ARB);
	}
}


void MapChunk::draw()
{
	if (!gWorld->frustum.intersects(vmin,vmax)) return;
	float mydist = (gWorld->camera - vcenter).length() - r;
	//if (mydist > gWorld->mapdrawdistance2) return;
	if (mydist > gWorld->culldistance) {
		if (gWorld->uselowlod) this->drawNoDetail();
		return;
	}
	visible = true;

	if (nTextures==0) return;

	if (!hasholes) {
		bool highres = gWorld->drawhighres;
		if (highres) {
			highres = mydist < gWorld->highresdistance2;
		}
		if (highres) {
			strip = gWorld->mapstrip2;
			striplen = stripsize2;
		} else {
			strip = gWorld->mapstrip;
			striplen = stripsize;
		}
	}

	// setup vertex buffers
	glBindBufferARB(GL_ARRAY_BUFFER_ARB, vertices);
	glVertexPointer(3, GL_FLOAT, 0, 0);
	glBindBufferARB(GL_ARRAY_BUFFER_ARB, normals);
	glNormalPointer(GL_FLOAT, 0, 0);
	// ASSUME: texture coordinates set up already

	// first pass: base texture
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, textures[0]);

	glActiveTextureARB(GL_TEXTURE1_ARB);
	glDisable(GL_TEXTURE_2D);

	drawPass(animated[0]);

	if (nTextures>1) {
		//glDepthFunc(GL_EQUAL); // GL_LEQUAL is fine too...?
		glDepthMask(GL_FALSE);
	}

	// additional passes: if required
	for (int i=0; i<nTextures-1; i++) {
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, textures[i+1]);
		// this time, use blending:
		glActiveTextureARB(GL_TEXTURE1_ARB);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, alphamaps[i]);

		drawPass(animated[i+1]);

	}

	if (nTextures>1) {
		//glDepthFunc(GL_LEQUAL);
		glDepthMask(GL_TRUE);
	}
	
	// shadow map
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_LIGHTING);

	Vec3D shc = gWorld->skies->colorSet[SHADOW_COLOR] * 0.3f;
	//glColor4f(0,0,0,1);
	glColor4f(shc.x,shc.y,shc.z,1);

	glActiveTextureARB(GL_TEXTURE1_ARB);
	glBindTexture(GL_TEXTURE_2D, shadow);
	glEnable(GL_TEXTURE_2D);

	drawPass(0);

	glEnable(GL_LIGHTING);
	glColor4f(1,1,1,1);

	/*
	//////////////////////////////////
	// debugging tile flags:
	GLfloat tcols[8][4] = {	{1,1,1,1},
		{1,0,0,1}, {1, 0.5f, 0, 1}, {1, 1, 0, 1},
		{0,1,0,1}, {0,1,1,1}, {0,0,1,1}, {0.8f, 0, 1,1}
	};
	glPushMatrix();
	glDisable(GL_CULL_FACE);
	glDisable(GL_TEXTURE_2D);
	glTranslatef(xbase, ybase, zbase);
	for (int i=0; i<8; i++) {
		int v = 1 << (7-i);
		for (int j=0; j<4; j++) {
			if (animated[j] & v) {
				glBegin(GL_TRIANGLES);
				glColor4fv(tcols[i]);

				glVertex3f(i*2.0f, 2.0f, j*2.0f);
				glVertex3f(i*2.0f+1.0f, 2.0f, j*2.0f);
				glVertex3f(i*2.0f+0.5f, 4.0f, j*2.0f);

				glEnd();
			}
		}
	}
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_CULL_FACE);
	glColor4f(1,1,1,1);
	glPopMatrix();
	*/
}

void MapChunk::drawNoDetail()
{
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glDisable(GL_TEXTURE_2D);
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_LIGHTING);

	glColor3fv(gWorld->skies->colorSet[FOG_COLOR]);
	//glColor3f(1,0,0);
	//glDisable(GL_FOG);

	// low detail version
	glBindBufferARB(GL_ARRAY_BUFFER_ARB, vertices);
	glVertexPointer(3, GL_FLOAT, 0, 0);
	glDisableClientState(GL_NORMAL_ARRAY);
	glDrawElements(GL_TRIANGLE_STRIP, stripsize, GL_UNSIGNED_SHORT, gWorld->mapstrip);
	glEnableClientState(GL_NORMAL_ARRAY);

	glColor4f(1,1,1,1);
	//glEnable(GL_FOG);

	glEnable(GL_LIGHTING);
	glActiveTextureARB(GL_TEXTURE1_ARB);
	glEnable(GL_TEXTURE_2D);
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glEnable(GL_TEXTURE_2D);
}



void MapChunk::drawWater()
{
	// TODO: figure out how water really works

	/*
	
	// (fake) WATER
	if (haswater) {
		glActiveTextureARB(GL_TEXTURE1_ARB);
		glDisable(GL_TEXTURE_2D);
		glActiveTextureARB(GL_TEXTURE0_ARB);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, gWorld->water);

		const float wr = 1.0f;

		//glDepthMask(GL_FALSE);
		glBegin(GL_QUADS);
		
		//glColor4f(1.0f,1.0f,1.0f,0.95f);
		glColor4f(1,1,1,1); // fuck you alpha blending
		
		glNormal3f(0,1,0);
		glTexCoord2f(0,0);
		glVertex3f(xbase, waterlevel, zbase);
		glTexCoord2f(0,wr);
		glVertex3f(xbase, waterlevel, zbase+CHUNKSIZE);
		glTexCoord2f(wr,wr);
		glVertex3f(xbase+CHUNKSIZE, waterlevel, zbase+CHUNKSIZE);
		glTexCoord2f(wr,0);
		glVertex3f(xbase+CHUNKSIZE, waterlevel, zbase);
		glEnd();

		//glColor4f(1,1,1,1);
		//glDepthMask(GL_TRUE);

	}

	*/

	if (haswater) {
		lq->draw();
	}

}

void MapNode::draw()
{
	if (!gWorld->frustum.intersects(vmin,vmax)) return;
	for (int i=0; i<4; i++) children[i]->draw();
}

void MapNode::setup(MapTile *t)
{
	vmin = Vec3D( 9999999.0f, 9999999.0f, 9999999.0f);
	vmax = Vec3D(-9999999.0f,-9999999.0f,-9999999.0f);
	mt = t;
	if (size==2) {
		// children will be mapchunks
		children[0] = &(mt->chunks[py][px]);
		children[1] = &(mt->chunks[py][px+1]);
		children[2] = &(mt->chunks[py+1][px]);
		children[3] = &(mt->chunks[py+1][px+1]);
	} else {
		int half = size / 2;
		children[0] = new MapNode(px, py, half);
		children[1] = new MapNode(px+half, py, half);
		children[2] = new MapNode(px, py+half, half);
		children[3] = new MapNode(px+half, py+half, half);
		for (int i=0; i<4; i++) {
			children[i]->setup(mt);
		}
	}
	for (int i=0; i<4; i++) {
		if (children[i]->vmin.x < vmin.x) vmin.x = children[i]->vmin.x;
		if (children[i]->vmin.y < vmin.y) vmin.y = children[i]->vmin.y;
		if (children[i]->vmin.z < vmin.z) vmin.z = children[i]->vmin.z;
		if (children[i]->vmax.x > vmax.x) vmax.x = children[i]->vmax.x;
		if (children[i]->vmax.y > vmax.y) vmax.y = children[i]->vmax.y;
		if (children[i]->vmax.z > vmax.z) vmax.z = children[i]->vmax.z;
	}
}

void MapNode::cleanup()
{
	if (size>2) {
		for (int i=0; i<4; i++) {
			children[i]->cleanup();
			delete children[i];
		}
	}
}

MapChunk *MapTile::getChunk(unsigned int x, unsigned int z)
{
	assert(x < 16 && z < 16);
	return &chunks[z][x];
}
