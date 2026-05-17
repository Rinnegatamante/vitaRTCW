/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "../renderer/tr_local.h"
#include "../sys/sys_local.h"

#include <vitasdk.h>
#include "vitaGL.h"

/*
===============
GLimp_Shutdown
===============
*/
void GLimp_Shutdown( void )
{
	ri.IN_Shutdown();
}

/*
===============
GLimp_Minimize

Minimize the game so that user is back at the desktop
===============
*/
void GLimp_Minimize( void )
{
}


/*
===============
GLimp_LogComment
===============
*/
extern void log2file(const char *format, ...);
void GLimp_LogComment( char *comment )
{
#ifndef RELEASE
	log2file(comment);
#endif
}

#define R_MODE_FALLBACK 3 // 640 * 480

/*
===============
GLimp_Init

This routine is responsible for initializing the OS specific portions
of OpenGL
===============
*/
float *gVertexBuffer;
uint8_t *gColorBuffer;
float *gTexCoordBuffer;
uint8_t inited = 0;

typedef struct vidmode_s
{
	const char *description;
	int width, height;
	float pixelAspect;		// pixel width / height
} vidmode_t;
extern vidmode_t r_vidModes[];

uint32_t cur_width, cur_height;

SceUID rend_mutex_in, rend_mutex_out;

extern int renderThread(int argc, void *argv);

void GLimp_Init( qboolean coreContext)
{
	
	if (r_mode->integer < 0) r_mode->integer = 3;
	
	glConfig.vidWidth = r_vidModes[r_mode->integer].width;
	glConfig.vidHeight = r_vidModes[r_mode->integer].height;
	glConfig.colorBits = 32;
	glConfig.depthBits = 32;
	glConfig.stencilBits = 8;
	glConfig.displayFrequency = 60;
	glConfig.stereoEnabled = qfalse;
	
	glConfig.driverType = GLDRV_ICD;
	glConfig.hardwareType = GLHW_GENERIC;
	glConfig.deviceSupportsGamma = qfalse;
	glConfig.textureCompression = TC_S3TC;
	glConfig.textureEnvAddAvailable = qtrue;
	glConfig.windowAspect = ((float)r_vidModes[r_mode->integer].width) / ((float)r_vidModes[r_mode->integer].height);
	glConfig.isFullscreen = qtrue;
	
	if (!inited){
		cur_width = glConfig.vidWidth;
		cur_height = glConfig.vidHeight;
		rend_mutex_in = sceKernelCreateSema("rend_mutex_in", 0, 0, 1, NULL);
		rend_mutex_out = sceKernelCreateSema("rend_mutex_out", 0, 0, 1, NULL);
		SceUID rend_thid = sceKernelCreateThread("Renderer Thread", &renderThread, 0x10000100, 0x40000, 0, 0, NULL);
		sceKernelStartThread(rend_thid, 0, NULL);
		sceKernelWaitSema(rend_mutex_out, 1, NULL);
		sceKernelSignalSema(rend_mutex_out, 1);
		inited = 1;
		cur_width = glConfig.vidWidth;
		cur_height = glConfig.vidHeight;
	} else if (glConfig.vidWidth != cur_width) {
		// Changed resolution in game, restarting the game
		sceAppMgrLoadExec("app0:/eboot.bin", NULL, NULL);
	}

	strncpy(glConfig.vendor_string, glGetString(GL_VENDOR), sizeof(glConfig.vendor_string));
	strncpy(glConfig.renderer_string, glGetString(GL_RENDERER), sizeof(glConfig.renderer_string));
	strncpy(glConfig.version_string, glGetString(GL_VERSION), sizeof(glConfig.version_string));
	strncpy(glConfig.extensions_string, glGetString(GL_EXTENSIONS), sizeof(glConfig.extensions_string));
	
	qglClearColor( 0, 0, 0, 1 );	
}


/*
===============
GLimp_EndFrame

Responsible for doing a swapbuffers
===============
*/
void GLimp_EndFrame( void )
{
	vglSwapBuffers(GL_FALSE);
	vglIndexPointerDefault();
	gVertexBuffer = (float *)vglAllocFromScratch(1024 * 1024);
	gColorBuffer = (uint8_t *)vglAllocFromScratch(1024 * 1024);
	gTexCoordBuffer = (float *)vglAllocFromScratch(1024 * 1024);
}
