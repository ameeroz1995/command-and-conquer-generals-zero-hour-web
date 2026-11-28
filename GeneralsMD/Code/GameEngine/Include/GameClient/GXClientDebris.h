// GXClientDebris.h: drawing-only stand-in for generic debris that the logic no longer creates as game
// objects (clientDebris switch). The logic side fills one record per piece at the point it would have
// created the debris object and calls gxClientDebris(); everything here uses client random only, so it
// never changes the game. The record is read during the call only: pointers need not outlive it.
#pragma once

class ThingTemplate;
class FXList;
class AudioEventRTS;
class Matrix3D;

struct GXDebrisAnim { const char *initial, *flying, *final; };

struct GXClientDebris
{
	const char *model;              // model name (the one passed to setModelName)
	const ThingTemplate *tmpl;      // the debris template (GenericDebris)
	UnsignedInt color;              // okToChangeModelColor ? source indicator color : 0
	Int shadowType;
	const GXDebrisAnim *anims;      // all anim sets; one is picked here
	Int animCount;
	const FXList *fxFinal;
	const AudioEventRTS *bounceSound;
	Coord3D pos;                    // spawn position, offset already applied
	const Matrix3D *mtx;            // source transform, may be null
	Real orientation;
	Coord3D srcVelocity;            // source physics velocity, zero if none
	UnsignedInt disposition;        // DebrisDisposition bits as in ObjectCreationList.cpp
	Real dispositionIntensity;
	Real mass, extraBounciness;
	Real minMag, maxMag, minPitch, maxPitch;
	Real spinRate, yawRate, rollRate, pitchRate;   // as in the nugget: < 0 means not set
	Bool orientInForceDirection;
	UnsignedInt lifetimeFrames;     // override from the caller, 0 = use min/max
	UnsignedInt minFrames, maxFrames;
	Bool fadeIn, fadeOut;
};

void gxClientDebris(const GXClientDebris &d);
void gxClientDebrisUpdate();        // GameClient::update, once per client frame
void gxClientDebrisReset();         // GameClient::reset
