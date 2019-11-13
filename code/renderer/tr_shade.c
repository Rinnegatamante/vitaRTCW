/*
===========================================================================

Return to Castle Wolfenstein single player GPL Source Code
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company. 

This file is part of the Return to Castle Wolfenstein single player GPL Source Code (RTCW SP Source Code).  

RTCW SP Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

RTCW SP Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with RTCW SP Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the RTCW SP Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the RTCW SP Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

// tr_shade.c

#include "tr_local.h"

extern SceUID First_Mutex;
extern SceUID FirstEnd_Mutex;
extern SceUID Second_Mutex;
extern SceUID SecondEnd_Mutex;

shaderCommands_t *cp_src;

void (*copyFunc1)();
void (*copyFunc2)();

void multithreaded_copy(void (*f1)(), void (*f2)()) {
	copyFunc1 = f1;
	copyFunc2 = f2;
	sceKernelSignalSema(First_Mutex, 1);
	sceKernelSignalSema(Second_Mutex, 1);
	sceKernelWaitSema(FirstEnd_Mutex, 1, NULL);
	sceKernelWaitSema(SecondEnd_Mutex, 1, NULL);
}

/*

  THIS ENTIRE FILE IS BACK END

  This file deals with applying shaders to surface data in the tess struct.
*/

/*
==================
R_DrawElements

Optionally performs our own glDrawElements that looks for strip conditions
instead of using the single glDrawElements call that may be inefficient
without compiled vertex arrays.
==================
*/
static void R_DrawElements( int numIndexes, const glIndex_t *indexes ) {
	vglDrawObjects(GL_TRIANGLES, numIndexes, GL_TRUE);
}


/*
=============================================================

SURFACE SHADERS

=============================================================
*/

shaderCommands_t tess;
static qboolean setArraysOnce;

/*
=================
R_BindAnimatedImage

=================
*/
static void R_BindAnimatedImage( textureBundle_t *bundle ) {
	int64_t index;

	if ( bundle->isVideoMap ) {
		ri.CIN_RunCinematic( bundle->videoMapHandle );
		ri.CIN_UploadCinematic( bundle->videoMapHandle );
		return;
	}

	if ( bundle->numImageAnimations <= 1 ) {
		if ( bundle->isLightmap && ( backEnd.refdef.rdflags & RDF_SNOOPERVIEW ) ) {
			GL_Bind( tr.whiteImage );
		} else {
			GL_Bind( bundle->image[0] );
		}
		return;
	}

	// it is necessary to do this messy calc to make sure animations line up
	// exactly with waveforms of the same frequency
	index = tess.shaderTime * bundle->imageAnimationSpeed * FUNCTABLE_SIZE;
	index >>= FUNCTABLE_SIZE2;

	if ( index < 0 ) {
		index = 0;  // may happen with shader time offsets
	}

	// Windows x86 doesn't load renderer DLL with 64 bit modulus
	//index %= bundle->numImageAnimations;
	while ( index >= bundle->numImageAnimations ) {
		index -= bundle->numImageAnimations;
	}

	if ( bundle->isLightmap && ( backEnd.refdef.rdflags & RDF_SNOOPERVIEW ) ) {
		GL_Bind( tr.whiteImage );
	} else {
		GL_Bind( bundle->image[ index ] );
	}
}

/*
================
DrawTris
Draws triangle outlines for debugging
================
*/
static void DrawTris (shaderCommands_t *input) {
	GL_Bind( tr.whiteImage );
	qglColor3f (1,1,1);

	GL_State( GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE );
	qglDepthRange( 0, 0 );

	qglDisableClientState (GL_COLOR_ARRAY);
	qglDisableClientState (GL_TEXTURE_COORD_ARRAY);

	float *vertices = gVertexBuffer;
	int i;
	for (i=0;i<input->numIndexes;i++){
		memcpy(gVertexBuffer, input->xyz[input->indexes[i]], sizeof(vec3_t));
		gVertexBuffer += 3;
	}
	vglVertexPointerMapped(vertices);
	
	//->if (qglLockArraysEXT) {
	//->	qglLockArraysEXT(0, input->numVertexes);
	//->	GLimp_LogComment( "glLockArraysEXT\n" );
	//->}

	R_DrawElements( input->numIndexes, input->indexes );

	//->if (qglUnlockArraysEXT) {
	//->	qglUnlockArraysEXT();
	//->	GLimp_LogComment( "glUnlockArraysEXT\n" );
	//->}
	qglDepthRange( 0, 1 );
}


/*
================
DrawNormals
Draws vertex normals for debugging
================
*/
static void DrawNormals (shaderCommands_t *input) {
	int		i;
	vec3_t	temp;

	GL_Bind( tr.whiteImage );
	qglColor3f (1,1,1);
	qglDepthRange( 0, 0 );	// never occluded
	GL_State( GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE );

	float *vertices = gVertexBuffer;
	for (i = 0 ; i < input->numVertexes ; i++) {
		memcpy(gVertexBuffer, input->xyz[input->indexes[i]], sizeof(vec3_t));
		gVertexBuffer += 3;
		VectorMA (input->xyz[input->indexes[i]], 2, input->normal[input->indexes[i]], temp);
		memcpy(gVertexBuffer, temp, sizeof(vec3_t));
		gVertexBuffer += 3;
	}
	vglVertexPointerMapped(vertices);
	vglDrawObjects(GL_LINES, input->numVertexes * 2, GL_TRUE);

	qglDepthRange( 0, 1 );
}

/*
==============
RB_BeginSurface

We must set some things up before beginning any tesselation,
because a surface may be forced to perform a RB_End due
to overflow.
==============
*/
void RB_BeginSurface( shader_t *shader, int fogNum ) {

	shader_t *state = ( shader->remappedShader ) ? shader->remappedShader : shader;

	tess.ATI_tess = qfalse;     //----(SA)	added
	tess.numIndexes = 0;
	tess.numVertexes = 0;
	tess.shader = state;
	tess.fogNum = fogNum;
	tess.dlightBits = 0;        // will be OR'd in by surface functions
	tess.xstages = state->stages;
	tess.numPasses = state->numUnfoggedPasses;
	tess.currentStageIteratorFunc = state->optimalStageIteratorFunc;

	tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;
	if ( tess.shader->clampTime && tess.shaderTime >= tess.shader->clampTime ) {
		tess.shaderTime = tess.shader->clampTime;
	}
	// done.
}

void copyGeneric1() {
	int i;
	int range = cp_src->numIndexes / 2;
	float *texcoord = gTexCoordBuffer;
	float *vertices = gVertexBuffer;
	uint8_t *colorbuf = gColorBuffer;
	for (i = 0; i < range; i++) {
		memcpy(vertices, cp_src->xyz[cp_src->indexes[i]], sizeof(vec3_t));
		vertices += 3;
		memcpy(texcoord, cp_src->svars.texcoords[0][cp_src->indexes[i]], sizeof(vec2_t));
		texcoord += 2;
		memcpy(colorbuf, cp_src->svars.colors[cp_src->indexes[i]], sizeof(uint32_t));
		colorbuf += 4;
	}
}

void copyGeneric2() {
	int i;
	int range = cp_src->numIndexes / 2;
	float *texcoord = gTexCoordBuffer + (range * 2);
	float *vertices = gVertexBuffer + (range * 3);
	uint8_t *colorbuf = gColorBuffer + (range * 4);
	for (i = range; i < cp_src->numIndexes; i++) {
		memcpy(vertices, cp_src->xyz[cp_src->indexes[i]], sizeof(vec3_t));
		vertices += 3;
		memcpy(texcoord, cp_src->svars.texcoords[0][cp_src->indexes[i]], sizeof(vec2_t));
		texcoord += 2;
		memcpy(colorbuf, cp_src->svars.colors[cp_src->indexes[i]], sizeof(uint32_t));
		colorbuf += 4;
	}
}

void copyMultiTexture1() {
	int i;
	int range = cp_src->numIndexes / 2;
	float *texcoord = gTexCoordBuffer;
	float *texcoord2 = texcoord + (cp_src->numIndexes * 2);
	float *vertices = gVertexBuffer;
	uint8_t *colorbuf = gColorBuffer;
	for (i = 0; i < range; i++) {
		memcpy(vertices, cp_src->xyz[cp_src->indexes[i]], sizeof(vec3_t));
		vertices += 3;
		memcpy(texcoord, cp_src->svars.texcoords[0][cp_src->indexes[i]], sizeof(vec2_t));
		texcoord += 2;
		memcpy(texcoord2, cp_src->svars.texcoords[1][cp_src->indexes[i]], sizeof(vec2_t));
		texcoord2 += 2;
		memcpy(colorbuf, cp_src->svars.colors[cp_src->indexes[i]], sizeof(uint32_t));
		colorbuf += 4;
	}
}

void copyMultiTexture2() {
	int i;
	int range = cp_src->numIndexes / 2;
	float *texcoord = gTexCoordBuffer + (range * 2);
	float *texcoord2 = texcoord + (cp_src->numIndexes * 2);
	float *vertices = gVertexBuffer + (range * 3);
	uint8_t *colorbuf = gColorBuffer + (range * 4);
	for (i = range; i < cp_src->numIndexes; i++) {
		memcpy(vertices, cp_src->xyz[cp_src->indexes[i]], sizeof(vec3_t));
		vertices += 3;
		memcpy(texcoord, cp_src->svars.texcoords[0][cp_src->indexes[i]], sizeof(vec2_t));
		texcoord += 2;
		memcpy(texcoord2, cp_src->svars.texcoords[1][cp_src->indexes[i]], sizeof(vec2_t));
		texcoord2 += 2;
		memcpy(colorbuf, cp_src->svars.colors[cp_src->indexes[i]], sizeof(uint32_t));
		colorbuf += 4;
	}
}

/*
===================
DrawMultitextured

output = t0 * t1 or t0 + t1

t0 = most upstream according to spec
t1 = most downstream according to spec
===================
*/
static void DrawMultitextured( shaderCommands_t *input, int stage ) {
	shaderStage_t   *pStage;

	pStage = tess.xstages[stage];

	// Ridah
	if ( tess.shader->noFog && pStage->isFogged ) {
		R_FogOn();
	} else if ( tess.shader->noFog && !pStage->isFogged ) {
		R_FogOff(); // turn it back off
	} else if ( backEnd.projection2D ) {
		R_FogOff();
	} else {    // make sure it's on
		R_FogOn();
	}
	// done.

	GL_State( pStage->stateBits );

	// this is an ugly hack to work around a GeForce driver
	// bug with multitexture and clip planes
	if ( backEnd.viewParms.isPortal ) {
		qglPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	}

	//
	// base
	//
	GL_SelectTexture( 0 );
	
	cp_src = input;
	multithreaded_copy(copyMultiTexture1, copyMultiTexture2);
	
	vglVertexPointerMapped(gVertexBuffer);
	vglTexCoordPointerMapped(gTexCoordBuffer);
	vglColorPointerMapped(GL_UNSIGNED_BYTE, gColorBuffer);
	gVertexBuffer += 3 * input->numIndexes;
	gColorBuffer += 4 * input->numIndexes;
	gTexCoordBuffer += 2 * input->numIndexes;
	
	R_BindAnimatedImage( &pStage->bundle[0] );
	R_DrawElements( input->numIndexes, input->indexes );
	
	//
	// lightmap/secondary pass
	//
	GL_SelectTexture( 1 );
	/*qglEnable( GL_TEXTURE_2D );
	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );

	if ( r_lightmap->integer ) {
		GL_TexEnv( GL_REPLACE );
	} else {
		GL_TexEnv( tess.shader->multitextureEnv );
	}*/
	
	vglTexCoordPointerMapped(gTexCoordBuffer);
	gTexCoordBuffer += 2 * input->numIndexes;
	
	R_BindAnimatedImage( &pStage->bundle[1] );

	R_DrawElements( input->numIndexes, input->indexes );

	//
	// disable texturing on TEXTURE1, then select TEXTURE0
	//
	//qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
	//->qglDisable( GL_TEXTURE_2D );

	GL_SelectTexture( 0 );
}

float	texCoordsArray[SHADER_MAX_VERTEXES][2];
byte	colorArray[SHADER_MAX_VERTEXES][4];
glIndex_t	hitIndexes[SHADER_MAX_INDEXES];
int		numIndexes;

void copyDlightTexture1() {
	int i;
	int range = numIndexes / 2;
	float *texcoord = gTexCoordBuffer;
	uint8_t *colorbuf = gColorBuffer;
	for (i = 0; i < range; i++) {
		memcpy(texcoord, texCoordsArray[hitIndexes[i]], sizeof(vec2_t));
		texcoord += 2;
		memcpy(colorbuf, colorArray[hitIndexes[i]], sizeof(uint32_t));
		colorbuf += 4;
	}
}

void copyDlightTexture2() {
	int i;
	int range = numIndexes / 2;
	float *texcoord = gTexCoordBuffer + (range * 2);
	uint8_t *colorbuf = gColorBuffer + (range * 4);
	for (i = range; i < numIndexes; i++) {
		memcpy(texcoord, texCoordsArray[hitIndexes[i]], sizeof(vec2_t));
		texcoord += 2;
		memcpy(colorbuf, colorArray[hitIndexes[i]], sizeof(uint32_t));
		colorbuf += 4;
	}
}

/*
===================
ProjectDlightTexture

Perform dynamic lighting with another rendering pass
===================
*/
static void ProjectDlightTexture_scalar( void ) {
	int		i, l;
	vec3_t	origin;
	float	*texCoords;
	byte	*colors;
	byte	clipBits[SHADER_MAX_VERTEXES];
	float	scale;
	float	radius;
	vec3_t	floatColor;
	float	modulate = 0.0f;

	if ( !backEnd.refdef.num_dlights ) {
		return;
	}

	if ( backEnd.refdef.rdflags & RDF_SNOOPERVIEW ) {  // no dlights for snooper
		return;
	}

	for ( l = 0 ; l < backEnd.refdef.num_dlights ; l++ ) {
		dlight_t	*dl;

		if ( !( tess.dlightBits & ( 1 << l ) ) ) {
			continue;	// this surface definately doesn't have any of this light
		}
		texCoords = texCoordsArray[0];
		colors = colorArray[0];

		dl = &backEnd.refdef.dlights[l];
		VectorCopy( dl->transformed, origin );
		radius = dl->radius;
		scale = 1.0f / radius;

		if(r_greyscale->integer)
		{
			float luminance;

			luminance = LUMA(dl->color[0], dl->color[1], dl->color[2]) * 255.0f;
			floatColor[0] = floatColor[1] = floatColor[2] = luminance;
		}
		else if(r_greyscale->value)
		{
			float luminance;
			
			luminance = LUMA(dl->color[0], dl->color[1], dl->color[2]) * 255.0f;
			floatColor[0] = LERP(dl->color[0] * 255.0f, luminance, r_greyscale->value);
			floatColor[1] = LERP(dl->color[1] * 255.0f, luminance, r_greyscale->value);
			floatColor[2] = LERP(dl->color[2] * 255.0f, luminance, r_greyscale->value);
		}
		else
		{
			floatColor[0] = dl->color[0] * 255.0f;
			floatColor[1] = dl->color[1] * 255.0f;
			floatColor[2] = dl->color[2] * 255.0f;
		}

		for ( i = 0 ; i < tess.numVertexes ; i++, texCoords += 2, colors += 4 ) {
			int		clip = 0;
			vec3_t	dist;
			
			VectorSubtract( origin, tess.xyz[i], dist );

			backEnd.pc.c_dlightVertexes++;

			texCoords[0] = 0.5f + dist[0] * scale;
			texCoords[1] = 0.5f + dist[1] * scale;

			if( !r_dlightBacks->integer &&
					// dist . tess.normal[i]
					( dist[0] * tess.normal[i][0] +
					dist[1] * tess.normal[i][1] +
					dist[2] * tess.normal[i][2] ) < 0.0f ) {
				clip = 63;
			} else {
				if ( texCoords[0] < 0.0f ) {
					clip |= 1;
				} else if ( texCoords[0] > 1.0f ) {
					clip |= 2;
				}
				if ( texCoords[1] < 0.0f ) {
					clip |= 4;
				} else if ( texCoords[1] > 1.0f ) {
					clip |= 8;
				}
				texCoords[0] = texCoords[0];
				texCoords[1] = texCoords[1];

				// modulate the strength based on the height and color
				if ( dist[2] > radius ) {
					clip |= 16;
					modulate = 0.0f;
				} else if ( dist[2] < -radius ) {
					clip |= 32;
					modulate = 0.0f;
				} else {
					dist[2] = Q_fabs(dist[2]);
					if ( dist[2] < radius * 0.5f ) {
						modulate = 1.0f;
					} else {
						modulate = 2.0f * (radius - dist[2]) * scale;
					}
				}
			}
			clipBits[i] = clip;
			colors[0] = ri.ftol(floatColor[0] * modulate);
			colors[1] = ri.ftol(floatColor[1] * modulate);
			colors[2] = ri.ftol(floatColor[2] * modulate);
			colors[3] = 255;
		}

		// build a list of triangles that need light
		numIndexes = 0;
		for ( i = 0 ; i < tess.numIndexes ; i += 3 ) {
			int		a, b, c;

			a = tess.indexes[i];
			b = tess.indexes[i+1];
			c = tess.indexes[i+2];
			if ( clipBits[a] & clipBits[b] & clipBits[c] ) {
				continue;	// not lighted
			}
			hitIndexes[numIndexes] = a;
			hitIndexes[numIndexes+1] = b;
			hitIndexes[numIndexes+2] = c;
			numIndexes += 3;
		}

		if ( !numIndexes ) {
			continue;
		}

		qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
		qglEnableClientState( GL_COLOR_ARRAY );
		
		multithreaded_copy(copyDlightTexture1, copyDlightTexture2);

		vglColorPointerMapped(GL_UNSIGNED_BYTE, gTexCoordBuffer);
		vglTexCoordPointerMapped(gColorBuffer);
		gTexCoordBuffer += numIndexes * 2;
		gColorBuffer += numIndexes * 4;

		//----(SA) creating dlight shader to allow for special blends or alternate dlight texture
		{
			shader_t *dls = dl->dlshader;
			if ( dls ) {
				for ( i = 0; i < dls->numUnfoggedPasses; i++ )
				{
					shaderStage_t *stage = dls->stages[i];
					R_BindAnimatedImage( &dls->stages[i]->bundle[0] );
					GL_State( stage->stateBits | GLS_DEPTHFUNC_EQUAL );
					R_DrawElements( numIndexes, hitIndexes );
					backEnd.pc.c_totalIndexes += numIndexes;
					backEnd.pc.c_dlightIndexes += numIndexes;
				}

			} else
			{
				R_FogOff();

				GL_Bind( tr.dlightImage );
				// include GLS_DEPTHFUNC_EQUAL so alpha tested surfaces don't add light
				// where they aren't rendered
				GL_State( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
				R_DrawElements( numIndexes, hitIndexes );
				backEnd.pc.c_totalIndexes += numIndexes;
				backEnd.pc.c_dlightIndexes += numIndexes;

				// Ridah, overdraw lights several times, rather than sending
				//	multiple lights through
				for ( i = 0; i < dl->overdraw; i++ ) {
					R_DrawElements( numIndexes, hitIndexes );
					backEnd.pc.c_totalIndexes += numIndexes;
					backEnd.pc.c_dlightIndexes += numIndexes;
				}

				R_FogOn();
			}
		}
	}
}

static void ProjectDlightTexture( void ) {
	ProjectDlightTexture_scalar();
}

void copyFogPass1() {
	int i;
	int range = tess.numIndexes / 2;
	float *texcoord = gTexCoordBuffer;
	uint8_t *colorbuf = gColorBuffer;
	for (i = 0; i < range; i++) {
		memcpy(texcoord, tess.svars.texcoords[0][tess.indexes[i]], sizeof(vec2_t));
		texcoord += 2;
		memcpy(colorbuf, tess.svars.colors[tess.indexes[i]], sizeof(uint32_t));
		colorbuf += 4;
	}
}

void copyFogPass2() {
	int i;
	int range = tess.numIndexes / 2;
	float *texcoord = gTexCoordBuffer + (range * 2);
	uint8_t *colorbuf = gColorBuffer + (range * 4);
	for (i = range; i < tess.numIndexes; i++) {
		memcpy(texcoord, tess.svars.texcoords[0][tess.indexes[i]], sizeof(vec2_t));
		texcoord += 2;
		memcpy(colorbuf, tess.svars.colors[tess.indexes[i]], sizeof(uint32_t));
		colorbuf += 4;
	}
}

/*
===================
RB_FogPass

Blends a fog texture on top of everything else
===================
*/
static void RB_FogPass( void ) {
	fog_t       *fog;
	int i;

	if ( tr.refdef.rdflags & RDF_SNOOPERVIEW ) { // no fog pass in snooper
		return;
	}

	qglEnableClientState( GL_COLOR_ARRAY );
	qglEnableClientState( GL_TEXTURE_COORD_ARRAY);

	fog = tr.world->fogs + tess.fogNum;

	for ( i = 0; i < tess.numVertexes; i++ ) {
		*( int * )&tess.svars.colors[i] = fog->colorInt;
	}

	RB_CalcFogTexCoords( ( float * ) tess.svars.texcoords[0] );

	GL_Bind( tr.fogImage );

	if ( tess.shader->fogPass == FP_EQUAL ) {
		GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL );
	} else {
		GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
	}

	multithreaded_copy(copyFogPass1, copyFogPass2);
	
	vglColorPointerMapped(GL_UNSIGNED_BYTE, gColorBuffer);
	vglTexCoordPointerMapped(gTexCoordBuffer);
	gColorBuffer += 4 * tess.numIndexes;
	gTexCoordBuffer += 2 * tess.numIndexes;
	
	R_DrawElements( tess.numIndexes, tess.indexes );
}

/*
===============
ComputeColors
===============
*/
static void ComputeColors( shaderStage_t *pStage ) {
	int i;

	//
	// rgbGen
	//
	switch ( pStage->rgbGen )
	{
	case CGEN_IDENTITY:
		memset( tess.svars.colors, 0xff, tess.numVertexes * 4 );
		break;
	default:
	case CGEN_IDENTITY_LIGHTING:
		memset( tess.svars.colors, tr.identityLightByte, tess.numVertexes * 4 );
		break;
	case CGEN_LIGHTING_DIFFUSE:
		RB_CalcDiffuseColor( ( unsigned char * ) tess.svars.colors );
		break;
	case CGEN_EXACT_VERTEX:
		memcpy( tess.svars.colors, tess.vertexColors, tess.numVertexes * sizeof( tess.vertexColors[0] ) );
		break;
	case CGEN_CONST:
		for ( i = 0; i < tess.numVertexes; i++ ) {
			*(int *)tess.svars.colors[i] = *(int *)pStage->constantColor;
		}
		break;
	case CGEN_VERTEX:
		if ( tr.identityLight == 1 ) {
			memcpy( tess.svars.colors, tess.vertexColors, tess.numVertexes * sizeof( tess.vertexColors[0] ) );
		} else
		{
			for ( i = 0; i < tess.numVertexes; i++ )
			{
				tess.svars.colors[i][0] = tess.vertexColors[i][0] * tr.identityLight;
				tess.svars.colors[i][1] = tess.vertexColors[i][1] * tr.identityLight;
				tess.svars.colors[i][2] = tess.vertexColors[i][2] * tr.identityLight;
				tess.svars.colors[i][3] = tess.vertexColors[i][3];
			}
		}
		break;
	case CGEN_ONE_MINUS_VERTEX:
		if ( tr.identityLight == 1 ) {
			for ( i = 0; i < tess.numVertexes; i++ )
			{
				tess.svars.colors[i][0] = 255 - tess.vertexColors[i][0];
				tess.svars.colors[i][1] = 255 - tess.vertexColors[i][1];
				tess.svars.colors[i][2] = 255 - tess.vertexColors[i][2];
			}
		} else
		{
			for ( i = 0; i < tess.numVertexes; i++ )
			{
				tess.svars.colors[i][0] = ( 255 - tess.vertexColors[i][0] ) * tr.identityLight;
				tess.svars.colors[i][1] = ( 255 - tess.vertexColors[i][1] ) * tr.identityLight;
				tess.svars.colors[i][2] = ( 255 - tess.vertexColors[i][2] ) * tr.identityLight;
			}
		}
		break;
	case CGEN_FOG:
	{
		fog_t       *fog;

		fog = tr.world->fogs + tess.fogNum;

		for ( i = 0; i < tess.numVertexes; i++ ) {
			*( int * )&tess.svars.colors[i] = fog->colorInt;
		}
	}
	break;
	case CGEN_WAVEFORM:
		RB_CalcWaveColor( &pStage->rgbWave, ( unsigned char * ) tess.svars.colors );
		break;
	case CGEN_ENTITY:
		RB_CalcColorFromEntity( ( unsigned char * ) tess.svars.colors );
		break;
	case CGEN_ONE_MINUS_ENTITY:
		RB_CalcColorFromOneMinusEntity( ( unsigned char * ) tess.svars.colors );
		break;
	}

	//
	// alphaGen
	//
	switch ( pStage->alphaGen )
	{
	case AGEN_SKIP:
		break;
	case AGEN_IDENTITY:
		if ( pStage->rgbGen != CGEN_IDENTITY ) {
			if ( ( pStage->rgbGen == CGEN_VERTEX && tr.identityLight != 1 ) ||
				 pStage->rgbGen != CGEN_VERTEX ) {
				for ( i = 0; i < tess.numVertexes; i++ ) {
					tess.svars.colors[i][3] = 0xff;
				}
			}
		}
		break;
	case AGEN_CONST:
		if ( pStage->rgbGen != CGEN_CONST ) {
			for ( i = 0; i < tess.numVertexes; i++ ) {
				tess.svars.colors[i][3] = pStage->constantColor[3];
			}
		}
		break;
	case AGEN_WAVEFORM:
		RB_CalcWaveAlpha( &pStage->alphaWave, ( unsigned char * ) tess.svars.colors );
		break;
	case AGEN_LIGHTING_SPECULAR:
		RB_CalcSpecularAlpha( ( unsigned char * ) tess.svars.colors );
		break;
	case AGEN_ENTITY:
		RB_CalcAlphaFromEntity( ( unsigned char * ) tess.svars.colors );
		break;
	case AGEN_ONE_MINUS_ENTITY:
		RB_CalcAlphaFromOneMinusEntity( ( unsigned char * ) tess.svars.colors );
		break;
		// Ridah
	case AGEN_NORMALZFADE:
	{
		float alpha, range, lowest, highest, dot;
		vec3_t worldUp;
		qboolean zombieEffect = qfalse;

		if ( VectorCompare( backEnd.currentEntity->e.fireRiseDir, vec3_origin ) ) {
			VectorSet( backEnd.currentEntity->e.fireRiseDir, 0, 0, 1 );
		}

		if ( backEnd.currentEntity->e.hModel ) {    // world surfaces dont have an axis
			VectorRotate( backEnd.currentEntity->e.fireRiseDir, backEnd.currentEntity->e.axis, worldUp );
		} else {
			VectorCopy( backEnd.currentEntity->e.fireRiseDir, worldUp );
		}

		lowest = pStage->zFadeBounds[0];
		if ( lowest == -1000 ) {    // use entity alpha
			lowest = backEnd.currentEntity->e.shaderTime;
			zombieEffect = qtrue;
		}
		highest = pStage->zFadeBounds[1];
		if ( highest == -1000 ) {   // use entity alpha
			highest = backEnd.currentEntity->e.shaderTime;
			zombieEffect = qtrue;
		}
		range = highest - lowest;
		for ( i = 0; i < tess.numVertexes; i++ ) {
			dot = DotProduct( tess.normal[i], worldUp );

			// special handling for Zombie fade effect
			if ( zombieEffect ) {
				alpha = (float)backEnd.currentEntity->e.shaderRGBA[3] * ( dot + 1.0 ) / 2.0;
				alpha += ( 2.0 * (float)backEnd.currentEntity->e.shaderRGBA[3] ) * ( 1.0 - ( dot + 1.0 ) / 2.0 );
				if ( alpha > 255.0 ) {
					alpha = 255.0;
				} else if ( alpha < 0.0 ) {
					alpha = 0.0;
				}
				tess.svars.colors[i][3] = (byte)( alpha );
				continue;
			}

			if ( dot < highest ) {
				if ( dot > lowest ) {
					if ( dot < lowest + range / 2 ) {
						alpha = ( (float)pStage->constantColor[3] * ( ( dot - lowest ) / ( range / 2 ) ) );
					} else {
						alpha = ( (float)pStage->constantColor[3] * ( 1.0 - ( ( dot - lowest - range / 2 ) / ( range / 2 ) ) ) );
					}
					if ( alpha > 255.0 ) {
						alpha = 255.0;
					} else if ( alpha < 0.0 ) {
						alpha = 0.0;
					}

					// finally, scale according to the entity's alpha
					if ( backEnd.currentEntity->e.hModel ) {
						alpha *= (float)backEnd.currentEntity->e.shaderRGBA[3] / 255.0;
					}

					tess.svars.colors[i][3] = (byte)( alpha );
				} else {
					tess.svars.colors[i][3] = 0;
				}
			} else {
				tess.svars.colors[i][3] = 0;
			}
		}
	}
	break;
		// done.
	case AGEN_VERTEX:
		if ( pStage->rgbGen != CGEN_VERTEX ) {
			for ( i = 0; i < tess.numVertexes; i++ ) {
				tess.svars.colors[i][3] = tess.vertexColors[i][3];
			}
		}
		break;
	case AGEN_ONE_MINUS_VERTEX:
		for ( i = 0; i < tess.numVertexes; i++ )
		{
			tess.svars.colors[i][3] = 255 - tess.vertexColors[i][3];
		}
		break;
	case AGEN_PORTAL:
	{
		unsigned char alpha;

		for ( i = 0; i < tess.numVertexes; i++ )
		{
			float len;
			vec3_t v;

			VectorSubtract( tess.xyz[i], backEnd.viewParms.or.origin, v );
			len = VectorLength( v );

			len /= tess.shader->portalRange;

			if ( len < 0 ) {
				alpha = 0;
			} else if ( len > 1 )   {
				alpha = 0xff;
			} else
			{
				alpha = len * 0xff;
			}

			tess.svars.colors[i][3] = alpha;
		}
	}
	break;
	}

	//
	// fog adjustment for colors to fade out as fog increases
	//
	if ( tess.fogNum ) {
		switch ( pStage->adjustColorsForFog )
		{
		case ACFF_MODULATE_RGB:
			RB_CalcModulateColorsByFog( ( unsigned char * ) tess.svars.colors );
			break;
		case ACFF_MODULATE_ALPHA:
			RB_CalcModulateAlphasByFog( ( unsigned char * ) tess.svars.colors );
			break;
		case ACFF_MODULATE_RGBA:
			RB_CalcModulateRGBAsByFog( ( unsigned char * ) tess.svars.colors );
			break;
		case ACFF_NONE:
			break;
		}
	}

	// if in greyscale rendering mode turn all color values into greyscale.
	if(r_greyscale->integer)
	{
		int scale;
		for(i = 0; i < tess.numVertexes; i++)
		{
			scale = LUMA(tess.svars.colors[i][0], tess.svars.colors[i][1], tess.svars.colors[i][2]);
			tess.svars.colors[i][0] = tess.svars.colors[i][1] = tess.svars.colors[i][2] = scale;
		}
	}
	else if(r_greyscale->value)
	{
		float scale;
		
		for(i = 0; i < tess.numVertexes; i++)
		{
			scale = LUMA(tess.svars.colors[i][0], tess.svars.colors[i][1], tess.svars.colors[i][2]);
			tess.svars.colors[i][0] = LERP(tess.svars.colors[i][0], scale, r_greyscale->value);
			tess.svars.colors[i][1] = LERP(tess.svars.colors[i][1], scale, r_greyscale->value);
			tess.svars.colors[i][2] = LERP(tess.svars.colors[i][2], scale, r_greyscale->value);
		}
	}
}

/*
===============
ComputeTexCoords
===============
*/
static void ComputeTexCoords( shaderStage_t *pStage ) {
	int i;
	int b;

	for ( b = 0; b < NUM_TEXTURE_BUNDLES; b++ ) {
		int tm;

		//
		// generate the texture coordinates
		//
		switch ( pStage->bundle[b].tcGen )
		{
		case TCGEN_IDENTITY:
			memset( tess.svars.texcoords[b], 0, sizeof( float ) * 2 * tess.numVertexes );
			break;
		case TCGEN_TEXTURE:
			for ( i = 0 ; i < tess.numVertexes ; i++ ) {
				tess.svars.texcoords[b][i][0] = tess.texCoords[i][0][0];
				tess.svars.texcoords[b][i][1] = tess.texCoords[i][0][1];
			}
			break;
		case TCGEN_LIGHTMAP:
			for ( i = 0 ; i < tess.numVertexes ; i++ ) {
				tess.svars.texcoords[b][i][0] = tess.texCoords[i][1][0];
				tess.svars.texcoords[b][i][1] = tess.texCoords[i][1][1];
			}
			break;
		case TCGEN_VECTOR:
			for ( i = 0 ; i < tess.numVertexes ; i++ ) {
				tess.svars.texcoords[b][i][0] = DotProduct( tess.xyz[i], pStage->bundle[b].tcGenVectors[0] );
				tess.svars.texcoords[b][i][1] = DotProduct( tess.xyz[i], pStage->bundle[b].tcGenVectors[1] );
			}
			break;
		case TCGEN_FOG:
			RB_CalcFogTexCoords( ( float * ) tess.svars.texcoords[b] );
			break;
		case TCGEN_ENVIRONMENT_MAPPED:
			RB_CalcEnvironmentTexCoords( ( float * ) tess.svars.texcoords[b] );
			break;
		case TCGEN_FIRERISEENV_MAPPED:
			RB_CalcFireRiseEnvTexCoords( ( float * ) tess.svars.texcoords[b] );
			break;
		case TCGEN_BAD:
			return;
		}

		//
		// alter texture coordinates
		//
		for ( tm = 0; tm < pStage->bundle[b].numTexMods ; tm++ ) {
			switch ( pStage->bundle[b].texMods[tm].type )
			{
			case TMOD_NONE:
				tm = TR_MAX_TEXMODS;        // break out of for loop
				break;

			case TMOD_SWAP:
				RB_CalcSwapTexCoords( ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_TURBULENT:
				RB_CalcTurbulentTexCoords( &pStage->bundle[b].texMods[tm].wave,
										   ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_ENTITY_TRANSLATE:
				RB_CalcScrollTexCoords( backEnd.currentEntity->e.shaderTexCoord,
										( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_SCROLL:
				RB_CalcScrollTexCoords( pStage->bundle[b].texMods[tm].scroll,
										( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_SCALE:
				RB_CalcScaleTexCoords( pStage->bundle[b].texMods[tm].scale,
									   ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_STRETCH:
				RB_CalcStretchTexCoords( &pStage->bundle[b].texMods[tm].wave,
										 ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_TRANSFORM:
				RB_CalcTransformTexCoords( &pStage->bundle[b].texMods[tm],
										   ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_ROTATE:
				RB_CalcRotateTexCoords( pStage->bundle[b].texMods[tm].rotateSpeed,
										( float * ) tess.svars.texcoords[b] );
				break;

			default:
				ri.Error( ERR_DROP, "ERROR: unknown texmod '%d' in shader '%s'", pStage->bundle[b].texMods[tm].type, tess.shader->name );
				break;
			}
		}
	}
}



extern void R_Fog( glfog_t *curfog );

/*
==============
SetIteratorFog
	set the fog parameters for this pass
==============
*/
void SetIteratorFog( void ) {
	// changed for problem when you start the game with r_fastsky set to '1'
//	if(r_fastsky->integer || backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) {
	if ( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) {
		R_FogOff();
		return;
	}

	if ( backEnd.refdef.rdflags & RDF_DRAWINGSKY ) {
		if ( glfogsettings[FOG_SKY].registered ) {
			R_Fog( &glfogsettings[FOG_SKY] );
		} else {
			R_FogOff();
		}

		return;
	}

	if ( skyboxportal && backEnd.refdef.rdflags & RDF_SKYBOXPORTAL ) {
		if ( glfogsettings[FOG_PORTALVIEW].registered ) {
			R_Fog( &glfogsettings[FOG_PORTALVIEW] );
		} else {
			R_FogOff();
		}
	} else {
		if ( glfogNum > FOG_NONE ) {
			R_Fog( &glfogsettings[FOG_CURRENT] );
		} else {
			R_FogOff();
		}
	}
}

/*
** RB_IterateStagesGeneric
*/
static void RB_IterateStagesGeneric( shaderCommands_t *input ) {
	int stage;

	for ( stage = 0; stage < MAX_SHADER_STAGES; stage++ )
	{
		shaderStage_t *pStage = tess.xstages[stage];

		if ( !pStage ) {
			break;
		}

		ComputeColors( pStage );
		ComputeTexCoords( pStage );

		qglEnableClientState( GL_COLOR_ARRAY );
		
		//
		// do multitexture
		//
		if ( pStage->bundle[1].image[0] != 0 ) {
			DrawMultitextured( input, stage );
		} else
		{
			int fadeStart, fadeEnd;

			cp_src = input;
			multithreaded_copy(copyGeneric1, copyGeneric2);
			
			vglVertexPointerMapped(gVertexBuffer);
			vglColorPointerMapped(GL_UNSIGNED_BYTE, gColorBuffer);
			vglTexCoordPointerMapped(gTexCoordBuffer);
			gTexCoordBuffer += cp_src->numIndexes * 2;
			gColorBuffer += cp_src->numIndexes * 4;
			gVertexBuffer += cp_src->numIndexes * 3;

			//
			// set state
			//
			R_BindAnimatedImage( &pStage->bundle[0] );

			// Ridah, per stage fogging (detail textures)
			if ( tess.shader->noFog && pStage->isFogged ) {
				R_FogOn();
			} else if ( tess.shader->noFog && !pStage->isFogged ) {
				R_FogOff(); // turn it back off
			} else if ( backEnd.projection2D ) {
				R_FogOff();
			} else {    // make sure it's on
				R_FogOn();
			}
			// done.

			//----(SA)	fading model stuff
			fadeStart = backEnd.currentEntity->e.fadeStartTime;

			if ( fadeStart ) {
				fadeEnd = backEnd.currentEntity->e.fadeEndTime;
				if ( fadeStart > tr.refdef.time ) {       // has not started to fade yet
					GL_State( pStage->stateBits );
				} else
				{
					int i;
					unsigned int tempState;
					float alphaval;

					if ( fadeEnd < tr.refdef.time ) {     // entity faded out completely
						continue;
					}

					alphaval = (float)( fadeEnd - tr.refdef.time ) / (float)( fadeEnd - fadeStart );

					tempState = pStage->stateBits;
					// remove the current blend, and don't write to Z buffer
					tempState &= ~( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS | GLS_DEPTHMASK_TRUE );
					// set the blend to src_alpha, dst_one_minus_src_alpha
					tempState |= ( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
					GL_State( tempState );
					GL_Cull( CT_FRONT_SIDED );
					// modulate the alpha component of each vertex in the render list
					for ( i = 0; i < tess.numVertexes; i++ ) {
						tess.svars.colors[i][0] *= alphaval;
						tess.svars.colors[i][1] *= alphaval;
						tess.svars.colors[i][2] *= alphaval;
						tess.svars.colors[i][3] *= alphaval;
					}
				}
			} else {
				GL_State( pStage->stateBits );
			}
			//----(SA)	end

			//
			// draw
			//
			R_DrawElements( input->numIndexes, input->indexes );
		}
		// allow skipping out to show just lightmaps during development
		if ( r_lightmap->integer && ( pStage->bundle[0].isLightmap || pStage->bundle[1].isLightmap ) ) {
			break;
		}
	}
}


/*
** RB_StageIteratorGeneric
*/
void RB_StageIteratorGeneric( void ) {
	shaderCommands_t *input;
	shader_t		*shader;

	input = &tess;
	shader = input->shader;

	RB_DeformTessGeometry();

	//
	// log this call
	//
	if ( r_logFile->integer ) {
		// don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment( va( "--- RB_StageIteratorGeneric( %s ) ---\n", tess.shader->name ) );
	}

	// set GL fog
	SetIteratorFog();

	//
	// set face culling appropriately
	//
	GL_Cull( shader->cullType );

	// set polygon offset if necessary
	if ( shader->polygonOffset ) {
		qglEnable( GL_POLYGON_OFFSET_FILL );
		qglPolygonOffset( r_offsetFactor->value, r_offsetUnits->value );
	}

	//
	// enable color and texcoord arrays after the lock if necessary
	//
	if ( !setArraysOnce ) {
		qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
		qglEnableClientState( GL_COLOR_ARRAY );
	}

	//
	// call shader function
	//
	RB_IterateStagesGeneric( input );

	//
	// now do any dynamic lighting needed
	//
	if ( tess.dlightBits && tess.shader->sort <= SS_OPAQUE
		 && !( tess.shader->surfaceFlags & ( SURF_NODLIGHT | SURF_SKY ) ) ) {
		ProjectDlightTexture();
	}

	//
	// now do fog
	//
	if ( tess.fogNum && tess.shader->fogPass ) {
		RB_FogPass();
	}

	//
	// reset polygon offset
	//
	if ( shader->polygonOffset ) {
		qglDisable( GL_POLYGON_OFFSET_FILL );
	}

}

void copyVertexLit1() {
	int i;
	int range = tess.numIndexes / 2;
	float *texcoord = gTexCoordBuffer;
	float *vertices = gVertexBuffer;
	uint8_t *colorbuf = gColorBuffer;
	for (i = 0; i < range; i++) {
		memcpy(vertices, tess.xyz[tess.indexes[i]], sizeof(vec3_t));
		vertices += 3;
		memcpy(texcoord, tess.texCoords[tess.indexes[i]][0], sizeof(vec2_t));
		texcoord += 2;
		memcpy(colorbuf, tess.svars.colors[tess.indexes[i]], sizeof(uint32_t));
		colorbuf += 4;
	}
}

void copyVertexLit2() {
	int i;
	int range = tess.numIndexes / 2;
	float *texcoord = gTexCoordBuffer + (range * 2);
	float *vertices = gVertexBuffer + (range * 3);
	uint8_t *colorbuf = gColorBuffer + (range * 4);
	for (i = range; i < tess.numIndexes; i++) {
		memcpy(vertices, tess.xyz[tess.indexes[i]], sizeof(vec3_t));
		vertices += 3;
		memcpy(texcoord, tess.texCoords[tess.indexes[i]][0], sizeof(vec2_t));
		texcoord += 2;
		memcpy(colorbuf, tess.svars.colors[tess.indexes[i]], sizeof(uint32_t));
		colorbuf += 4;
	}
}

/*
** RB_StageIteratorVertexLitTexture
*/
void RB_StageIteratorVertexLitTexture( void ) {
	shaderCommands_t *input;
	shader_t        *shader;

	input = &tess;

	shader = input->shader;

	//
	// compute colors
	//
	RB_CalcDiffuseColor( ( unsigned char * ) tess.svars.colors );

	//
	// log this call
	//
	if ( r_logFile->integer ) {
		// don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment( va( "--- RB_StageIteratorVertexLitTexturedUnfogged( %s ) ---\n", tess.shader->name ) );
	}


	// set GL fog
	SetIteratorFog();

	//
	// set face culling appropriately
	//
	GL_Cull( shader->cullType );

	//
	// set arrays and lock
	//
	multithreaded_copy(copyVertexLit1, copyVertexLit2);
	
	vglColorPointerMapped(GL_UNSIGNED_BYTE, gColorBuffer);
	vglTexCoordPointerMapped(gTexCoordBuffer);
	vglVertexPointerMapped(gVertexBuffer);
	gColorBuffer += 4 * tess.numIndexes;
	gTexCoordBuffer += 2 * tess.numIndexes;
	gVertexBuffer += 3 * tess.numIndexes;

	//
	// call special shade routine
	//
	R_BindAnimatedImage( &tess.xstages[0]->bundle[0] );
	GL_State( tess.xstages[0]->stateBits );
	R_DrawElements( input->numIndexes, input->indexes );

	//
	// now do any dynamic lighting needed
	//
	if ( tess.dlightBits && tess.shader->sort <= SS_OPAQUE ) {
		ProjectDlightTexture();
	}

	//
	// now do fog
	//
	if ( tess.fogNum && tess.shader->fogPass ) {
		RB_FogPass();
	}

}

//define	REPLACE_MODE

void copyMultiLit1() {
	int i;
	int range = tess.numIndexes / 2;
	float *texcoord = gTexCoordBuffer;
	float *texcoord2 = texcoord + tess.numIndexes * 2;
	float *vertices = gVertexBuffer;
	for (i = 0; i < range; i++) {
		memcpy(vertices, tess.xyz[tess.indexes[i]], sizeof(vec3_t));
		vertices += 3;
		memcpy(texcoord, tess.texCoords[tess.indexes[i]][0], sizeof(vec2_t));
		texcoord += 2;
		memcpy(texcoord2, tess.texCoords[tess.indexes[i]][1], sizeof(vec2_t));
		texcoord2 += 2;
	}
}

void copyMultiLit2() {
	int i;
	int range = tess.numIndexes / 2;
	float *texcoord = gTexCoordBuffer + (range * 2);
	float *vertices = gVertexBuffer + (range * 3);
	float *texcoord2 = texcoord + tess.numIndexes * 2;
	for (i = 0; i < range; i++) {
		memcpy(vertices, tess.xyz[tess.indexes[i]], sizeof(vec3_t));
		vertices += 3;
		memcpy(texcoord, tess.texCoords[tess.indexes[i]][0], sizeof(vec2_t));
		texcoord += 2;
		memcpy(texcoord2, tess.texCoords[tess.indexes[i]][1], sizeof(vec2_t));
		texcoord2 += 2;
	}
}

void RB_StageIteratorLightmappedMultitexture( void ) {
	shaderCommands_t *input;
	shader_t		*shader;

	input = &tess;
	shader = input->shader;

	//
	// log this call
	//
	if ( r_logFile->integer ) {
		// don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment( va( "--- RB_StageIteratorLightmappedMultitexture( %s ) ---\n", tess.shader->name ) );
	}

	// set GL fog
	SetIteratorFog();

	//
	// set face culling appropriately
	//
	GL_Cull( shader->cullType );

	//
	// set color, pointers, and lock
	//
	GL_State( GLS_DEFAULT );
	
	qglEnableClientState( GL_COLOR_ARRAY );	
	vglColorPointerMapped(GL_UNSIGNED_BYTE, gColorBuffer255);

	//
	// select base stage
	//
	GL_SelectTexture( 0 );

	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
	R_BindAnimatedImage( &tess.xstages[0]->bundle[0] );
	
	multithreaded_copy(copyMultiLit1, copyMultiLit2);
	
	vglVertexPointerMapped(gVertexBuffer);
	vglTexCoordPointerMapped(gTexCoordBuffer);
	gVertexBuffer += 3 * tess.numIndexes;
	gTexCoordBuffer += 2 * tess.numIndexes;
	
	R_DrawElements( input->numIndexes, input->indexes );

	//
	// configure second stage
	//
	GL_SelectTexture( 1 );

//----(SA)	modified for snooper
	if ( tess.xstages[0]->bundle[1].isLightmap && ( backEnd.refdef.rdflags & RDF_SNOOPERVIEW ) ) {
		GL_Bind( tr.whiteImage );
	} else {
		R_BindAnimatedImage( &tess.xstages[0]->bundle[1] );
	}

	R_BindAnimatedImage( &tess.xstages[0]->bundle[1] );
	
	vglTexCoordPointerMapped(gTexCoordBuffer);
	gTexCoordBuffer += 2 * tess.numIndexes;

	R_DrawElements( input->numIndexes, input->indexes );

	GL_SelectTexture( 0 );
#ifdef REPLACE_MODE
	GL_TexEnv( GL_MODULATE );
	qglShadeModel( GL_SMOOTH );
#endif

	//
	// now do any dynamic lighting needed
	//
	if ( tess.dlightBits && tess.shader->sort <= SS_OPAQUE ) {
		ProjectDlightTexture();
	}

	//
	// now do fog
	//
	if ( tess.fogNum && tess.shader->fogPass ) {
		RB_FogPass();
	}

}

/*
** RB_EndSurface
*/
void RB_EndSurface( void ) {
	shaderCommands_t *input;

	input = &tess;

	if ( input->numIndexes == 0 ) {
		return;
	}

	if ( input->indexes[SHADER_MAX_INDEXES - 1] != 0 ) {
		ri.Error( ERR_DROP, "RB_EndSurface() - SHADER_MAX_INDEXES hit" );
	}
	if ( input->xyz[SHADER_MAX_VERTEXES - 1][0] != 0 ) {
		ri.Error( ERR_DROP, "RB_EndSurface() - SHADER_MAX_VERTEXES hit" );
	}

	if ( tess.shader == tr.shadowShader ) {
		RB_ShadowTessEnd();
		return;
	}

	// for debugging of sort order issues, stop rendering after a given sort value
	if ( r_debugSort->integer && r_debugSort->integer < tess.shader->sort ) {
		return;
	}

	if ( skyboxportal ) {
		// world
		if ( !( backEnd.refdef.rdflags & RDF_SKYBOXPORTAL ) ) {
			if ( tess.currentStageIteratorFunc == RB_StageIteratorSky ) {  // don't process these tris at all
				return;
			}
		}
		// portal sky
		else {
			if ( !drawskyboxportal ) {
				if ( !( tess.currentStageIteratorFunc == RB_StageIteratorSky ) ) {  // /only/ process sky tris
					return;
				}
			}
		}
	}

	//
	// update performance counters
	//
	backEnd.pc.c_shaders++;
	backEnd.pc.c_vertexes += tess.numVertexes;
	backEnd.pc.c_indexes += tess.numIndexes;
	backEnd.pc.c_totalIndexes += tess.numIndexes * tess.numPasses;

	//
	// call off to shader specific tess end function
	//
	tess.currentStageIteratorFunc();

	//
	// draw debugging stuff
	//
	if ( r_showtris->integer ) {
		DrawTris( input );
	}
	if ( r_shownormals->integer ) {
		DrawNormals( input );
	}


	// clear shader so we can tell we don't have any unclosed surfaces
	tess.numIndexes = 0;

	GLimp_LogComment( "----------\n" );
}

