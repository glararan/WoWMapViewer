#include "world.h"

#include <cassert>

using namespace std;



World *gWorld=0;


World::World(const char* name):basename(name)
{
	//::gWorld = this;

	gLog("\nLoading world %s\n", name);

	for (int i=0; i<MAPTILECACHESIZE; i++) maptilecache[i] = 0;

	autoheight = false;

	init();
	skies = 0;
	ol = 0;

	// don't load map objects while still on the menu screen
	//initDisplay();
}


void World::init()
{
	for (int j=0; j<64; j++) {
		for (int i=0; i<64; i++) {
			lowrestiles[j][i] = 0;
		}
	}

	gnWMO = 0;
	nMaps = 0;
	water = 0;
	char fn[256];
	sprintf(fn,"World\\Maps\\%s\\%s.wdt", basename.c_str(), basename.c_str());

	time = 1450;
	animtime = 0;

	ex = ez = -1;
	loading = false;

	drawfog = false;

	memset(maps,0,sizeof(maps));

	MPQFile f(fn);

	char fourcc[5];
	size_t size;

	while (!f.isEof()) {
		f.read(fourcc,4);
		f.read(&size, 4);

		flipcc(fourcc);
		fourcc[4] = 0;

		size_t nextpos = f.getPos() + size;

		if (!strcmp(fourcc,"MAIN")) {
			for (int j=0; j<64; j++) {
				for (int i=0; i<64; i++) {
					int d;
					f.read(&d, 4);
					if (d) {
						maps[j][i] = true;
						nMaps++;
					} else maps[j][i] = false;
					f.read(&d, 4);
				}
			}
		}
		else if (!strcmp(fourcc,"MODF")) {
			// global wmo instance data
			gnWMO = (int)size / 64;
			// WMOS and WMO-instances are handled below in initWMOs()
		}
		f.seek((int)nextpos);
	}
	f.close();

	mapstrip = 0;
	mapstrip2 = 0;

	minimap = 0;
	if (nMaps) initMinimap();
}


void World::initMinimap()
{
	glGenTextures(1, &minimap);

	// zomg, data on the stack!!1
	//int texbuf[512][512];
	unsigned int *texbuf = new unsigned int[512*512];
	memset(texbuf,0,512*512*4);

	// as alpha is unused, maybe I should try 24bpp? :(
	short tilebuf[17*17];

	int ofsbuf[64][64];

	char fn[256];
	sprintf(fn,"World\\Maps\\%s\\%s.wdl", basename.c_str(), basename.c_str());

	MPQFile f(fn);
	f.seek(0x14);
	f.read(ofsbuf,64*64*4);

	for (int j=0; j<64; j++) {
		for (int i=0; i<64; i++) {
			if (ofsbuf[j][i]) {
				f.seek(ofsbuf[j][i]+8);
				// read height values ^_^

				/*
				short *sp = tilebuf;
				for (int z=0; z<33; z++) {
					f.read(sp, 2 * ( (z%2) ? 16 : 17 ));
					sp += 17;
				}*/
				/*
				fucking win. in the .adt files, height maps are stored in 9-8-9-8-... interleaved order.
				here, apparently, a 17x17 map is stored followed by a 16x16 map.
				yay for consistency.
				I'm only using the 17x17 map here.
				*/
				f.read(tilebuf,17*17*2);

				// make minimap
				// for a 512x512 minimap texture, and 64x64 tiles, one tile is 8x8 pixels
				for (int z=0; z<8; z++) {
					for (int x=0; x<8; x++) {
						short hval = tilebuf[(z*2)*17+x*2]; // for now

						// make rgb from height value
						unsigned char r,g,b;
						if (hval < 0) {
							// water = blue
							if (hval < -511) hval = -511;
							hval /= -2;
							r = g = 0;
							b = 255 - hval;
						} else {
							// above water = should apply a palette :(
							/*
							float fh = hval / 1600.0f;
							if (fh > 1.0f) fh = 1.0f;
							unsigned char c = (unsigned char) (fh * 255.0f);
							r = g = b = c;
							*/

							// green: 20,149,7		0-600
							// brown: 137, 84, 21	600-1200
							// gray: 96, 96, 96		1200-1600
							// white: 255, 255, 255
							unsigned char r1,r2,g1,g2,b1,b2;
							float t;

							if (hval < 600) {
								r1 = 20;
								r2 = 137;
								g1 = 149;
								g2 = 84;
								b1 = 7;
								b2 = 21;
								t = hval / 600.0f;
							}
							else if (hval < 1200) {
								r2 = 96;
								r1 = 137;
								g2 = 96;
								g1 = 84;
								b2 = 96;
								b1 = 21;
								t = (hval-600) / 600.0f;
							}
							else /*if (hval < 1600)*/ {
								r1 = 96;
								r2 = 255;
								g1 = 96;
								g2 = 255;
								b1 = 96;
								b2 = 255;
								if (hval >= 1600) hval = 1599;
								t = (hval-1200) / 600.0f;
							}

							// TODO: add a regular palette here

							r = (unsigned char)(r2*t + r1*(1.0f-t));
							g = (unsigned char)(g2*t + g1*(1.0f-t));
							b = (unsigned char)(b2*t + b1*(1.0f-t));
						}

						texbuf[(j*8+z)*512 + i*8+x] = (r) | (g<<8) | (b<<16) | (255 << 24);
					}
				}
			}
		}
	}
	
	/*
	// TEMP - draw sky areas
	skies = new Skies(basename.c_str());
	skies->debugDraw(texbuf, 512);
	delete skies;
	*/

	glBindTexture(GL_TEXTURE_2D, minimap);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 512, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE, texbuf);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);

	delete[] texbuf;
	f.close();
}


void World::initLowresTerrain()
{
	char fn[256];
	sprintf(fn,"World\\Maps\\%s\\%s.wdl", basename.c_str(), basename.c_str());
	short tilebuf[17*17];
	short tilebuf2[16*16];
	Vec3D lowres[17][17];
	Vec3D lowsub[16][16];
	int ofsbuf[64][64];

	MPQFile f(fn);
	f.seek(0x14);
	f.read(ofsbuf,64*64*4);

	for (int j=0; j<64; j++) {
		for (int i=0; i<64; i++) {
			if (ofsbuf[j][i]) {
				f.seek(ofsbuf[j][i]+8);
				f.read(tilebuf,17*17*2);
				f.read(tilebuf2,16*16*2);
			
				for (int y=0; y<17; y++) {
					for (int x=0; x<17; x++) {
						lowres[y][x] = Vec3D(TILESIZE*(i+x/16.0f), tilebuf[y*17+x], TILESIZE*(j+y/16.0f));
					}
				}
				for (int y=0; y<16; y++) {
					for (int x=0; x<16; x++) {
						lowsub[y][x] = Vec3D(TILESIZE*(i+(x+0.5f)/16.0f), tilebuf2[y*16+x], TILESIZE*(j+(y+0.5f)/16.0f));
					}
				}

				GLuint dl;
				dl = glGenLists(1);
				glNewList(dl, GL_COMPILE);
				/*
				// draw tiles 16x16?
				glBegin(GL_TRIANGLE_STRIP);
				for (int y=0; y<16; y++) {
					// end jump
					if (y>0) glVertex3fv(lowres[y][0]);
					for (int x=0; x<17; x++) {
                        glVertex3fv(lowres[y][x]);
                        glVertex3fv(lowres[y+1][x]);
					}
                    // start jump
					if (y<15) glVertex3fv(lowres[y+1][16]);
				}
				glEnd();
				*/
				// draw tiles 17*17+16*16
				glBegin(GL_TRIANGLES);
				for (int y=0; y<16; y++) {
					for (int x=0; x<16; x++) {
						glVertex3fv(lowres[y][x]);		glVertex3fv(lowsub[y][x]);	glVertex3fv(lowres[y][x+1]);
						glVertex3fv(lowres[y][x+1]);	glVertex3fv(lowsub[y][x]);	glVertex3fv(lowres[y+1][x+1]);
						glVertex3fv(lowres[y+1][x+1]);	glVertex3fv(lowsub[y][x]);	glVertex3fv(lowres[y+1][x]);
						glVertex3fv(lowres[y+1][x]);	glVertex3fv(lowsub[y][x]);	glVertex3fv(lowres[y][x]);
					}
				}
				glEnd();
				glEndList();
				lowrestiles[j][i] = dl;
			}
		}
	}
}

GLuint gdetailtexcoords=0, galphatexcoords=0;

void initGlobalVBOs()
{
	if (gdetailtexcoords==0 && galphatexcoords==0) {

		GLuint detailtexcoords, alphatexcoords;

		Vec2D temp[mapbufsize], *vt;
		float tx,ty;
		
		// init texture coordinates for detail map:
		vt = temp;
		const float detail_half = 0.5f * detail_size / 8.0f;
		for (int j=0; j<17; j++) {
			for (int i=0; i<((j%2)?8:9); i++) {
				tx = detail_size / 8.0f * i;
				ty = detail_size / 8.0f * j * 0.5f;
				if (j%2) {
					// offset by half
					tx += detail_half;
				}
				*vt++ = Vec2D(tx, ty);
			}
		}

		glGenBuffersARB(1, &detailtexcoords);
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, detailtexcoords);
		glBufferDataARB(GL_ARRAY_BUFFER_ARB, mapbufsize*2*sizeof(float), temp, GL_STATIC_DRAW_ARB);

		// init texture coordinates for alpha map:
		vt = temp;
		const float alpha_half = 0.5f * 1.0f / 8.0f;
		for (int j=0; j<17; j++) {
			for (int i=0; i<((j%2)?8:9); i++) {
				tx = 1.0f / 8.0f * i;
				ty = 1.0f / 8.0f * j * 0.5f;
				if (j%2) {
					// offset by half
					tx += alpha_half;
				}
				*vt++ = Vec2D(tx*0.95f, ty*0.95f);
			}
		}

		glGenBuffersARB(1, &alphatexcoords);
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, alphatexcoords);
		glBufferDataARB(GL_ARRAY_BUFFER_ARB, mapbufsize*2*sizeof(float), temp, GL_STATIC_DRAW_ARB);

		glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);


		gdetailtexcoords=detailtexcoords;
		galphatexcoords=alphatexcoords;
	}

}


void World::initDisplay()
{
	// temp code until I figure out water properly
	water = video.textures.add("XTextures\\river\\lake_c.10.blp");

	// default strip indices
	short *defstrip = new short[stripsize];
	for (int i=0; i<stripsize; i++) defstrip[i] = i; // note: this is ugly and should be handled in stripify
	mapstrip = new short[stripsize];
	stripify<short>(defstrip, mapstrip);
	delete[] defstrip;

	defstrip = new short[stripsize2];
	for (int i=0; i<stripsize2; i++) defstrip[i] = i; // note: this is ugly and should be handled in stripify
	mapstrip2 = new short[stripsize2];
	stripify2<short>(defstrip, mapstrip2);
	delete[] defstrip;

	initGlobalVBOs();
	detailtexcoords = gdetailtexcoords;
	alphatexcoords = galphatexcoords;

	highresdistance = 384.0f;
	mapdrawdistance = 998.0f;
	modeldrawdistance = 384.0f;
	doodaddrawdistance = 64.0f;

	oob = false;

	if (gnWMO > 0) initWMOs();

	skies = new Skies(basename.c_str(), gnWMO==0);

	ol = new OutdoorLighting("World\\dnc.db");

	initLowresTerrain();
}


void World::initWMOs()
{
	gnWMO = 0;
	char fn[256];
	sprintf(fn,"World\\Maps\\%s\\%s.wdt", basename.c_str(), basename.c_str());

	MPQFile f(fn);

	char fourcc[5];
	size_t size;

	while (!f.isEof()) {
		f.read(fourcc,4);
		f.read(&size, 4);

		flipcc(fourcc);
		fourcc[4] = 0;

		size_t nextpos = f.getPos() + size;

		if (!strcmp(fourcc,"MWMO")) {
			// global map objects
			if (size) {
				char *buf = new char[size];
				f.read(buf, size);
				char *p=buf;
				while (p<buf+size) {
					string path(p);
					p+=strlen(p)+1;
					
					wmomanager.add(path);
					gwmos.push_back(path);
				}
				delete[] buf;
			}
		}
		else if (!strcmp(fourcc,"MODF")) {
			// global wmo instance data
			gnWMO = (int)size / 64;
			for (int i=0; i<gnWMO; i++) {
				int id;
				f.read(&id, 4);
				WMO *wmo = (WMO*)wmomanager.items[wmomanager.get(gwmos[id])];
				WMOInstance inst(wmo, f);
				gwmois.push_back(inst);
			}
		}
		f.seek((int)nextpos);
	}
	f.close();
}

World::~World()
{
	for (int j=0; j<64; j++) {
		for (int i=0; i<64; i++) {
			if (lowrestiles[j][i]!=0) glDeleteLists(lowrestiles[j][i],1);
		}
	}

	for (int i=0; i<MAPTILECACHESIZE; i++) {
		if (maptilecache[i] != 0) delete maptilecache[i];
	}

	for (vector<string>::iterator it = gwmos.begin(); it != gwmos.end(); ++it) {
		wmomanager.delbyname(*it);
	}

	if (minimap) glDeleteTextures(1, &minimap);

	// temp code until I figure out water properly
	if (water) video.textures.del(water);

	if (skies) delete skies;
	if (ol) delete ol;

	if (mapstrip) delete[] mapstrip;
	if (mapstrip2) delete[] mapstrip2;

	gLog("Unloaded world %s\n", basename.c_str());
}

bool oktile(int i, int j)
{
	return i>=0 && j >= 0 && i<64 && j<64;
}

void World::enterTile(int x, int z)
{
	if (!oktile(x,z)) {
		oob = true;
		return;
	} else oob = !maps[z][x];

	cx = x;
	cz = z;
	for (int j=0; j<3; j++) {
		for (int i=0; i<3; i++) {
			current[j][i] = loadTile(x-1+i, z-1+j);
		}
	}
	if (autoheight && current[1][1]!=0 && current[1][1]->ok) {
		//Vec3D vc = (current[1][1]->topnode.vmax + current[1][1]->topnode.vmin) * 0.5f;
		Vec3D vc = current[1][1]->topnode.vmax;
		if (vc.y < 0) vc.y = 0;
		camera.y = vc.y + 50.0f;

		autoheight = false;
	}
}

MapTile *World::loadTile(int x, int z)
{
	if (!oktile(x,z) || !maps[z][x]) {
		//gLog("Tile %d,%d not in map\n", x, z);
		return 0;
	}

	int firstnull = MAPTILECACHESIZE;
	for (int i=0; i<MAPTILECACHESIZE; i++) {
		if ((maptilecache[i] != 0)  && (maptilecache[i]->x == x) && (maptilecache[i]->z == z)) {
            return maptilecache[i];
		}
		if (maptilecache[i] == 0 && i < firstnull) firstnull = i;
	}
	// ok we need to find a place in the cache
	if (firstnull == MAPTILECACHESIZE) {
		int score, maxscore = 0, maxidx = 0;
		// oh shit we need to throw away a tile
		for (int i=0; i<MAPTILECACHESIZE; i++) {
			score = abs(maptilecache[i]->x - cx) + abs(maptilecache[i]->z - cz);
			if (score>maxscore) {
				maxscore = score;
				maxidx = i;
			}
		}

		// maxidx is the winner (loser)
		delete maptilecache[maxidx];
		firstnull = maxidx;
	}

	// TODO: make a loader thread  or something :(

	char name[256];
	sprintf(name,"World\\Maps\\%s\\%s_%d_%d.adt", basename.c_str(), basename.c_str(), x, z);

	maptilecache[firstnull] = new MapTile(x,z,name);
	return maptilecache[firstnull];
}


void lightingDefaults()
{
	glLightf(GL_LIGHT0, GL_CONSTANT_ATTENUATION, 1);
	glLightf(GL_LIGHT0, GL_LINEAR_ATTENUATION, 0);
	glLightf(GL_LIGHT0, GL_QUADRATIC_ATTENUATION, 0);

	glEnable(GL_LIGHT0);
	// wtf
	glDisable(GL_LIGHT1);
	glDisable(GL_LIGHT2);
	glDisable(GL_LIGHT3);
	glDisable(GL_LIGHT4);
	glDisable(GL_LIGHT5);
	glDisable(GL_LIGHT6);
	glDisable(GL_LIGHT7);
}

/*
void myFakeLighting()
{
	GLfloat la = 0.5f;
	GLfloat ld = 1.0f;

	GLfloat LightAmbient[] = {la, la, la, 1.0f};
	GLfloat LightDiffuse[] = {ld, ld, ld, 1.0f};
	GLfloat LightPosition[] = {-10.0f, 20.0f, -10.0f, 0.0f};
	glLightfv(GL_LIGHT0, GL_AMBIENT, LightAmbient);
	glLightfv(GL_LIGHT0, GL_DIFFUSE, LightDiffuse);
	glLightfv(GL_LIGHT0, GL_POSITION,LightPosition);
}
*/

void World::outdoorLighting()
{
	Vec4D black(0,0,0,0);
	Vec4D ambient(skies->colorSet[LIGHT_GLOBAL_AMBIENT], 1);
	glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);

	float di = outdoorLightStats.dayIntensity, ni = outdoorLightStats.nightIntensity;
	di = 1;
	ni = 0;

	//Vec3D dd = outdoorLightStats.dayDir;
	// HACK: let's just keep the light source in place for now
	Vec4D pos(-1, 1, -1, 0);
	Vec4D col(skies->colorSet[LIGHT_GLOBAL_DIFFUSE] * di, 1); 
	glLightfv(GL_LIGHT0, GL_AMBIENT, black);
	glLightfv(GL_LIGHT0, GL_DIFFUSE, col);
	glLightfv(GL_LIGHT0, GL_POSITION, pos);
	
	/*
	Vec3D dd = outdoorLightStats.nightDir;
	Vec4D pos(-dd.x, -dd.z, dd.y, 0);
	Vec4D col(skies->colorSet[LIGHT_GLOBAL_DIFFUSE] * ni, 1); 
	glLightfv(GL_LIGHT1, GL_AMBIENT, black);
	glLightfv(GL_LIGHT1, GL_DIFFUSE, col);
	glLightfv(GL_LIGHT1, GL_POSITION, pos);
	*/
}

void World::outdoorLights(bool on)
{
	float di = outdoorLightStats.dayIntensity, ni = outdoorLightStats.nightIntensity;
	di = 1;
	ni = 0;

	if (on) {
		Vec4D ambient(skies->colorSet[LIGHT_GLOBAL_AMBIENT], 1);
		glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
		if (di>0) {
			glEnable(GL_LIGHT0);
		} else {
			glDisable(GL_LIGHT0);
		}
		if (ni>0) {
			glEnable(GL_LIGHT1);
		} else {
			glDisable(GL_LIGHT1);
		}
	} else {
		Vec4D ambient(0, 0, 0, 1);
		glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);
		glDisable(GL_LIGHT0);
		glDisable(GL_LIGHT1);
	}
}

void World::setupFog()
{
	if (drawfog) {

		//float fogdist = 357.0f; // minimum draw distance in wow
		//float fogdist = 777.0f; // maximum draw distance in wow
		// TODO: make this adjustable or something
		float fogdist = fogdistance;
		float fogstart = 0.5f;

		culldistance = fogdist;

		Vec4D fogcolor(skies->colorSet[FOG_COLOR], 1);
		glFogfv(GL_FOG_COLOR, fogcolor);
		// TODO: retreive fogstart and fogend from lights.lit somehow
		glFogf(GL_FOG_END, fogdist);
		glFogf(GL_FOG_START, fogdist * fogstart);

		glEnable(GL_FOG);
	} else {
		glDisable(GL_FOG);
		culldistance = mapdrawdistance;
	}
	culldistance2 = culldistance * culldistance;
}


void World::draw()
{
	WMOInstance::reset();
	modelmanager.resetAnim();

	glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);

	highresdistance2 = highresdistance * highresdistance;
	mapdrawdistance2 = mapdrawdistance * mapdrawdistance;
	modeldrawdistance2 = modeldrawdistance * modeldrawdistance;
	doodaddrawdistance2 = doodaddrawdistance * doodaddrawdistance;

	// setup camera
	// assume identity modelview matrix ^_^
	//glMatrixMode(GL_MODELVIEW);
	//glLoadIdentity();
	gluLookAt(camera.x,camera.y,camera.z, lookat.x,lookat.y,lookat.z, 0, 1, 0);

	// camera is set up
	frustum.retrieve();

	if (thirdperson) {
		Vec3D l = (lookat-camera).normalize();
		Vec3D nc = camera + Vec3D(0,300,0);
		Vec3D rt = (Vec3D(0,1,0) % l).normalize();
		Vec3D up = (l % rt).normalize();
		float fl = 256.0f;
		Vec3D fp = camera + l * fl;
		glLoadIdentity();
		gluLookAt(nc.x,nc.y,nc.z, camera.x,camera.y,camera.z, 1, 0, 0);
	}

	glDisable(GL_LIGHTING);
	glColor4f(1,1,1,1);

    //int tt = 1440;
	//if (modelmanager.v>0) {
	//	tt = (modelmanager.v *180 + 1440) % 2880;
	//}

	hadSky = false;
	for (int j=0; j<3; j++) {
		for (int i=0; i<3; i++) {
			if (oktile(i,j) && current[j][i] != 0) current[j][i]->drawSky();
			if (hadSky) break;
		}
		if (hadSky) break;
	}
	if (gnWMO && !hadSky) {
		for (int i=0; i<gnWMO; i++) {
			gwmois[i].wmo->drawSkybox();
			if (hadSky) break;
		}
	}
	glEnable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_FOG);

	int daytime = ((int)time)%2880;
	outdoorLightStats = ol->getLightStats(daytime);
	skies->initSky(camera, daytime);

	if (!hadSky) hadSky = skies->drawSky(camera);

	// clearing the depth buffer only - color buffer is/has been overwritten anyway
	// unless there is no sky OR skybox
	GLbitfield clearmask = GL_DEPTH_BUFFER_BIT;
	if (!hadSky) clearmask |= GL_COLOR_BUFFER_BIT;
	glClear(clearmask); 

	glDisable(GL_TEXTURE_2D);


	outdoorLighting();
	outdoorLights(true);

	glFogi(GL_FOG_MODE, GL_LINEAR);
	setupFog();

	// Draw verylowres heightmap
	if (drawfog && drawterrain) {
		glEnable(GL_CULL_FACE);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_LIGHTING);
		glColor3fv(this->skies->colorSet[FOG_COLOR]);
		//glColor3f(0,1,0);
		//glDisable(GL_FOG);
		const int lrr = 2;
		for (int i=cx-lrr; i<=cx+lrr; i++) {
			for (int j=cz-lrr; j<=cz+lrr; j++) {
				// TODO: some annoying visual artifacts when the verylowres terrain overlaps
				// maptiles that are close (1-off) - figure out how to fix.
				// still less annoying than hoels in the horizon when only 2-off verylowres tiles are drawn
				if ( !(i==cx&&j==cz) && oktile(i,j) && lowrestiles[j][i]) {
					glCallList(lowrestiles[j][i]);
				}
			}
		}
		//glEnable(GL_FOG);
	}

	// Draw height map
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_NORMAL_ARRAY);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL); // less z-fighting artifacts this way, I think
	glEnable(GL_LIGHTING);


	glEnable(GL_COLOR_MATERIAL);
	//glColorMaterial(GL_FRONT, GL_DIFFUSE);
	glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
	glColor4f(1,1,1,1);

	glEnable(GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glClientActiveTextureARB(GL_TEXTURE0_ARB);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glBindBufferARB(GL_ARRAY_BUFFER_ARB, detailtexcoords);
	glTexCoordPointer(2, GL_FLOAT, 0, 0);

	glClientActiveTextureARB(GL_TEXTURE1_ARB);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glBindBufferARB(GL_ARRAY_BUFFER_ARB, alphatexcoords);
	glTexCoordPointer(2, GL_FLOAT, 0, 0);

	glClientActiveTextureARB(GL_TEXTURE0_ARB);

	// height map w/ a zillion texture passes
	if (drawterrain) {
		for (int j=0; j<3; j++) {
			for (int i=0; i<3; i++) {
				uselowlod = drawfog;// && i==1 && j==1;
				if (oktile(i,j) && current[j][i] != 0) current[j][i]->draw();
			}
		}
	}

	glActiveTextureARB(GL_TEXTURE1_ARB);
	glDisable(GL_TEXTURE_2D);
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glEnable(GL_TEXTURE_2D);

	glDisable(GL_CULL_FACE);

	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);

	// gosh darn alpha blended evil
	for (int j=0; j<3; j++) {
		for (int i=0; i<3; i++) {
			if (drawterrain && oktile(i,j) && current[j][i] != 0)	current[j][i]->drawWater();
		}
	}
	glColor4f(1,1,1,1);
	glEnable(GL_BLEND);


	// unbind hardware buffers
	glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);


	glEnable(GL_CULL_FACE);

	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);

	// TEMP: for fucking around with lighting
	for (int i=0; i<8; i++) {
		GLuint light = GL_LIGHT0 + i;
		glLightf(light, GL_CONSTANT_ATTENUATION, l_const);
		glLightf(light, GL_LINEAR_ATTENUATION, l_linear);
		glLightf(light, GL_QUADRATIC_ATTENUATION, l_quadratic);
	}

	if (gnWMO) {
		oob = false;
		for (int i=0; i<gnWMO; i++) {
			gwmois[i].draw();
		}
	}
	
	// map objects
	for (int j=0; j<3; j++) {
		for (int i=0; i<3; i++) {
			if (oktile(i,j) && drawwmo && current[j][i] != 0) current[j][i]->drawObjects();
		}
	}

	outdoorLights(true);
	setupFog();

	glColor4f(1,1,1,1);
	//models
	for (int j=0; j<3; j++) {
		for (int i=0; i<3; i++) {
			if (oktile(i,j) && drawmodels && current[j][i] != 0) current[j][i]->drawModels();
		}
	}

	/*
	// temp frustum code
	glDisable(GL_LIGHTING);
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBegin(GL_TRIANGLES);
	glColor4f(0,1,0,0.5);
	glVertex3fv(camera);
	glVertex3fv(fp - rt * fl * 1.33f - up * fl);
	glVertex3fv(fp + rt * fl * 1.33f - up * fl);
	glColor4f(0,0,1,0.5);
	glVertex3fv(camera);
	fl *= 0.5f;
	glVertex3fv(fp - rt * fl * 1.33f + up * fl);
	glVertex3fv(fp + rt * fl * 1.33f + up * fl);
	glEnd();
	*/

	glColor4f(1,1,1,1);
	glDisable(GL_COLOR_MATERIAL);

	if (current[1][1] != 0 || oob) {
		if (oob || (camera.x<current[1][1]->xbase) || (camera.x>(current[1][1]->xbase+TILESIZE))
			|| (camera.z<current[1][1]->zbase) || (camera.z>(current[1][1]->zbase+TILESIZE)) )
		{
			ex = (int)(camera.x / TILESIZE);
			ez = (int)(camera.z / TILESIZE);

			loading = true;
		}
	}

}


void World::tick(float dt)
{
	if (loading) {
		if (ex!=-1 && ez!=-1) {
			enterTile(ex,ez);
		}
		ex = ez = -1;
		loading = false;
	}
	while (dt > 0.1f) {
		modelmanager.updateEmitters(0.1f);
		dt -= 0.1f;
	}
	modelmanager.updateEmitters(dt);
}

unsigned int World::getAreaID()
{
	MapTile *curTile;

	int mtx,mtz,mcx,mcz;
	mtx = (int) (camera.x / TILESIZE);
	mtz = (int) (camera.z / TILESIZE);
	mcx = (int) (fmod(camera.x, TILESIZE) / CHUNKSIZE);
	mcz = (int) (fmod(camera.z, TILESIZE) / CHUNKSIZE);

	if ((mtx<cx-1) || (mtx>cx+1) || (mtz<cz-1) || (mtz>cz+1)) return 0;
	
	curTile = current[mtz-cz+1][mtx-cx+1];
	if(curTile == 0) return 0;

	MapChunk *curChunk = curTile->getChunk(mcx, mcz);

	if(curChunk == 0) return 0;

	return curChunk->areaID;
}
