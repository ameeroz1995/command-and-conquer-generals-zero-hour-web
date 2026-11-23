// ============================================================================
// WebBenchmark.cpp — the battle benchmark behind Settings → RUN BENCHMARK (web port).
//
// One fixed, repeatable scene that every rendering change is measured on:
//   1. Battle, played like a real match: a 2 v 2 skirmish on Tournament Desert (fixed seed,
//      three brutal AIs building and fighting on their own). Each side gets a base of 300
//      structures (every buildable structure of every faction and general, power plants
//      first) and 800 units (every buildable infantry, vehicle and aircraft of every
//      faction and general, in turn) that arrive in four waves on three fronts and attack
//      the other base. The camera moves like a player's: it scrolls along the front, visits
//      both bases and turns; every 2 s the player selects another group (units near the
//      camera, all aircraft, one front's infantry, a single unit, the vehicles on screen)
//      and sends it somewhere else (the enemy base, its army, a structure, another front). Every special power on the bases (superweapons, then the generals'
//      strikes) is fired, alternating sides, at the other army and the other base.
//   2. Map tour: a few maps of different looks, each loaded alone, the camera visiting
//      four points; a thumbnail of each is kept so a broken map is seen at a glance.
// Frame times and draw calls are recorded per phase; the page shows them
// (window.gxBenchDone) and keeps the last result in localStorage.
//
// Only offline: refused while a LAN or internet game is running. Everything runs on the
// game thread; the page's call is forwarded there in the worker build.
// ============================================================================
#include <emscripten.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <malloc.h>
#include <unistd.h>
#include <cmath>
#include <string>
#include <vector>

#include "Lib/BaseType.h"
#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Science.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/KindOf.h"
#include "Common/RandomValue.h"
#include "Common/SpecialPower.h"
#include "GameClient/ControlBar.h"
#include "GameLogic/Weapon.h"
#include "GameClient/MapUtil.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Drawable.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/View.h"
#include "GameLogic/AI.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameNetwork/GameInfo.h"

#ifdef __EMSCRIPTEN_PTHREADS__
#include <emscripten/threading.h>
#endif

extern "C" int gx_dx8_draw_called;
extern "C" void (*gx_frame_hook)(double work_ms);
extern "C" double gx_stat_upload_bytes, gx_stat_triangles;
extern "C" unsigned gx_stat_gl_draws, gx_stat_programs, gx_stat_tex_binds;
extern "C" int gx_bench_shot;	// gles3_wrapper.cpp: 1 = send a thumbnail of the next frame to the page

namespace {

const char *const kBattleMap = "Tournament Desert";
const char *const kTourMaps[] = { "Alpine Assault", "Tournament Urban", "Dark Night", "Golden Oasis", "Eastern Everglades" };
const int kTourCount = sizeof(kTourMaps) / sizeof(kTourMaps[0]);

const int kUnitsPerSide = 800;
const int kStructuresPerSide = 300;
const int kPowerPlantsFirst = 30;			// so the superweapons are not shut down for lack of power
const UnsignedInt kBattleFrames = 1800;	// 60 s of game time
const double kBattleWallMs = 150000.0;	// slow machines stop here
const int kWaves = 4;								// the armies arrive in this many waves ...
const UnsignedInt kWaveEvery = 150;	// ... this many logic frames apart
const UnsignedInt kStrikesFrom = 450;	// strikes start once the armies have met
const UnsignedInt kStrikeEvery = 10;		// logic frames between strikes
const double kLoadWallMs = 120000.0;	// a map that has not loaded by then is reported as failed

enum Stage { IDLE, BATTLE_LOAD, BATTLE_RUN, TOUR_LOAD, TOUR_RUN, FINISH };

// One frame: time, CPU work, and what was sent to the graphics chip.
struct Sample { float ms, work; int draws; unsigned glDraws, programs, binds; float tris, upload; };
// Every 30 frames: memory and how much is in the world.
struct Slow { float heapMB, heapTopMB; int objects, particles; };
struct Phase { std::string name; std::vector<Sample> s; std::vector<Slow> slow; double wall0 = 0, wall1 = 0; UnsignedInt logic0 = 0, logic1 = 0; std::string note; };

struct Strike { ObjectID src; const SpecialPowerTemplate *power; int side; };

struct Bench {
	Stage stage = IDLE;
	bool withTour = true;
	double stageStart = 0;
	int tourIndex = 0;
	UnsignedInt frame0 = 0;
	int lastDraws = 0;
	double lastUpload = 0, lastTris = 0;
	unsigned lastGlDraws = 0, lastPrograms = 0, lastBinds = 0;
	std::vector<Phase> phases;
	Phase *cur = nullptr;
	std::vector<Strike> strikes;
	size_t nextStrike = 0;
	Coord3D baseCenter[2];
	std::vector<const ThingTemplate *> units;
	int wavesSent = 0;
	UnsignedInt lastOrder = 0xffffffff;
	Real cx = 0, cy = 0;
	int spawned[2] = {0, 0}, built[2] = {0, 0}, unitTypes = 0, structureTypes = 0, fired = 0;
	double setupMs = 0;
	int shotFor = -1;
	std::string setupNote;
	Player *side[2] = {nullptr, nullptr};
} B;

double now() { return emscripten_get_now(); }

void progress(const char *text)
{
	printf("GXBENCH %s (logic frame %u)\n", text, TheGameLogic ? TheGameLogic->getFrame() : 0);
	EM_ASM({ if (window.gxBenchProgress) window.gxBenchProgress(UTF8ToString($0)); }, text);
}

// ---------------------------------------------------------------------------- starting games

AsciiString findMap(const char *name)
{
	if (!TheMapCache) return AsciiString::TheEmptyString;
	TheMapCache->updateCache();
	AsciiString want = name; want.toLower();
	AsciiString suffix; suffix.format("\\%s.map", want.str());
	for (MapCache::iterator it = TheMapCache->begin(); it != TheMapCache->end(); ++it) {
		AsciiString key = it->first; key.toLower();
		if (key.getLength() >= suffix.getLength() && strcmp(key.str() + key.getLength() - suffix.getLength(), suffix.str()) == 0) return it->first;
	}
	return AsciiString::TheEmptyString;
}

bool startSkirmish(const char *mapName, bool withEnemy)
{
	AsciiString map = findMap(mapName);
	if (map.isEmpty()) return false;
	if (TheGameLogic->isInGame()) TheGameLogic->clearGameData(FALSE);
	if (!TheSkirmishGameInfo) TheSkirmishGameInfo = NEW SkirmishGameInfo;
	TheSkirmishGameInfo->init();
	TheSkirmishGameInfo->clearSlotList();
	TheSkirmishGameInfo->reset();
	TheSkirmishGameInfo->setLocalIP(TheSkirmishGameInfo->getSlot(0)->getIP());
	TheSkirmishGameInfo->enterGame();

	GameSlot me;
	me.setState(SLOT_PLAYER, UnicodeString(L"Benchmark"));
	me.setColor(0);
	me.setPlayerTemplate(ThePlayerTemplateStore->getTemplateNumByName("FactionAmerica"));
	me.setStartPos(0);
	me.setTeamNumber(-1);
	TheSkirmishGameInfo->setSlot(0, me);
	if (withEnemy) {
		// 2 v 2: an AI ally, two AI enemies (all hard), each a different faction
		const MapMetaData *mmd = TheMapCache->findMap(map);
		const int players = (mmd && mmd->m_numPlayers >= 4) ? 4 : 2;
		const char *faction[4] = { "FactionAmerica", "FactionChina", "FactionGLA", "FactionChina" };
		for (int i = 1; i < players; ++i) {
			GameSlot ai;
			ai.setState(SLOT_BRUTAL_AI);
			ai.setColor(i);
			ai.setPlayerTemplate(ThePlayerTemplateStore->getTemplateNumByName(faction[players == 4 ? (i == 1 ? 2 : i == 2 ? 1 : 3) : 1]));
			ai.setStartPos(i);
			ai.setTeamNumber(players == 4 ? (i == 1 ? 0 : 1) : -1);
			TheSkirmishGameInfo->setSlot(i, ai);
		}
		if (players == 4) { GameSlot me0 = *TheSkirmishGameInfo->getSlot(0); me0.setTeamNumber(0); TheSkirmishGameInfo->setSlot(0, me0); }
	}
	TheSkirmishGameInfo->setSeed(12345);	// same battle every run
	Money cash; cash.deposit(10000, FALSE, FALSE);
	TheSkirmishGameInfo->setStartingCash(cash);
	TheSkirmishGameInfo->setSuperweaponRestriction(0);
	TheSkirmishGameInfo->setMap(map);
	const MapMetaData *md = TheMapCache->findMap(map);
	TheSkirmishGameInfo->setMapCRC(md ? md->m_CRC : 0);
	TheSkirmishGameInfo->setMapSize(md ? md->m_filesize : 0);

	TheWritableGlobalData->m_mapName = map;
	TheSkirmishGameInfo->startGame(0);
	InitRandom(TheSkirmishGameInfo->getSeed());
	GameMessage *msg = TheMessageStream->appendMessage(GameMessage::MSG_NEW_GAME);
	msg->appendIntegerArgument(GAME_SKIRMISH);
	msg->appendIntegerArgument(DIFFICULTY_NORMAL);
	msg->appendIntegerArgument(0);
	msg->appendIntegerArgument(1000);	// no frame limit
	return true;
}

bool gameReady()
{
	return TheGameLogic->isInGame() && !TheGameLogic->isInShellGame() && !TheGameLogic->isLoadingMap() && TheGameLogic->getFrame() > 2 && ThePlayerList;
}

// ---------------------------------------------------------------------------- the battle

bool factionSide(const ThingTemplate *t)
{
	const AsciiString &s = t->getDefaultOwningSide();
	return s.startsWith("America") || s.startsWith("China") || s.startsWith("GLA");
}

void grantAllSciences(Player *p)
{
	std::vector<AsciiString> v = TheScienceStore->friend_getScienceNames();
	for (size_t i = 0; i < v.size(); ++i) {
		ScienceType st = TheScienceStore->getScienceFromInternalName(v[i]);
		if (st != SCIENCE_INVALID && TheScienceStore->isScienceGrantable(st)) p->grantScience(st);
	}
}

Object *spawn(const ThingTemplate *t, Player *p, Real x, Real y, Real angle)
{
	Object *o = TheThingFactory->newObject(t, p->getDefaultTeam());
	if (!o) return nullptr;
	Coord3D pos; pos.x = x; pos.y = y; pos.z = TheTerrainLogic->getGroundHeight(x, y);
	o->setPosition(&pos);
	o->setOrientation(angle);
	return o;
}

Coord3D groundAt(Real x, Real y)
{
	Coord3D p; p.x = x; p.y = y; p.z = TheTerrainLogic->getGroundHeight(x, y);
	return p;
}

void setupBattle()
{
	// us, and the first AI that is our enemy
	B.side[0] = ThePlayerList->getLocalPlayer();
	for (Int i = 0; i < ThePlayerList->getPlayerCount() && B.side[0]; ++i) {
		Player *p = ThePlayerList->getNthPlayer(i);
		if (p != B.side[0] && p->getPlayerType() == PLAYER_COMPUTER && B.side[0]->getRelationship(p->getDefaultTeam()) == ENEMIES) { B.side[1] = p; break; }
	}
	if (!B.side[0] || !B.side[1]) { B.setupNote = "no enemy player"; return; }
	grantAllSciences(B.side[0]);
	grantAllSciences(B.side[1]);

	// every buildable unit (aircraft too) and structure of every faction and general
	std::vector<const ThingTemplate *> structures, power;
	for (const ThingTemplate *t = TheThingFactory->firstTemplate(); t; t = t->friend_getNextTemplate()) {
		if (!factionSide(t) || t->friend_getBuildCost() <= 0 || t->getBuildable() == BSTATUS_NO) continue;
		if (t->isKindOf(KINDOF_STRUCTURE)) {
			if (t->isKindOf(KINDOF_FS_POWER)) power.push_back(t);
			structures.push_back(t);
		} else if (t->isKindOf(KINDOF_INFANTRY) || t->isKindOf(KINDOF_VEHICLE) || t->isKindOf(KINDOF_AIRCRAFT)) {
			B.units.push_back(t);
		}
	}
	B.unitTypes = (int)B.units.size();
	B.structureTypes = (int)structures.size();
	if (B.units.empty() || structures.empty()) { B.setupNote = "no templates"; return; }

	Region3D ext; TheTerrainLogic->getExtent(&ext);
	B.cx = (ext.lo.x + ext.hi.x) / 2; B.cy = (ext.lo.y + ext.hi.y) / 2;
	const Real half = std::min(ext.hi.x - ext.lo.x, ext.hi.y - ext.lo.y) / 2;
	// bases: 25 across, 12 deep, from 45% of the way out to the map edge
	const int cols = 25, rows = (kStructuresPerSide + cols - 1) / cols;
	const Real front = half * 0.45f, spacing = std::max(40.0f, std::min(80.0f, (half * 0.95f - front) / rows));
	for (int s = 0; s < 2; ++s) {
		const Real dir = s == 0 ? -1.0f : 1.0f;
		const Real face = s == 0 ? 0.0f : (Real)M_PI;
		for (int i = 0; i < kStructuresPerSide; ++i) {
			const ThingTemplate *t = (i < kPowerPlantsFirst && !power.empty()) ? power[i % power.size()]
				: structures[(i - kPowerPlantsFirst) % structures.size()];
			const int r = i / cols, c = i % cols;
			if (spawn(t, B.side[s], B.cx + dir * (front + r * spacing), B.cy + (c - cols / 2) * spacing, face)) B.built[s]++;
		}
		B.baseCenter[s] = groundAt(B.cx + dir * (front + rows * spacing / 2), B.cy);
	}

	// every special power on the bases, superweapons first, alternating sides
	std::vector<Strike> perSide[2];
	for (int s = 0; s < 2; ++s) {
		for (Object *o = TheGameLogic->getFirstObject(); o; o = o->getNextObject()) {
			if (o->getControllingPlayer() != B.side[s] || !o->isKindOf(KINDOF_STRUCTURE)) continue;
			for (BehaviorModule **m = o->getBehaviorModules(); m && *m; ++m) {
				SpecialPowerModuleInterface *sp = (*m)->getSpecialPower();
				if (!sp || !sp->getSpecialPowerTemplate()) continue;
				const SpecialPowerTemplate *tp = sp->getSpecialPowerTemplate();
				bool dup = false;
				for (const Strike &k : perSide[s]) if (k.power == tp) { dup = true; break; }
				if (!dup) perSide[s].push_back({ o->getID(), tp, s });
			}
		}
		std::stable_sort(perSide[s].begin(), perSide[s].end(), [](const Strike &a, const Strike &b) {
			return (a.power->getRadiusCursorRadius() > 150) > (b.power->getRadiusCursorRadius() > 150); });
	}
	for (size_t i = 0; i < std::max(perSide[0].size(), perSide[1].size()); ++i)
		for (int s = 0; s < 2; ++s) if (i < perSide[s].size()) B.strikes.push_back(perSide[s][i]);
}

// One wave per side: a quarter of the army, on one of three fronts, sent at the other base.
void sendWave(int wave)
{
	const int perWave = kUnitsPerSide / kWaves, cols = 20;
	Region3D ext; TheTerrainLogic->getExtent(&ext);
	const Real half = std::min(ext.hi.x - ext.lo.x, ext.hi.y - ext.lo.y) / 2;
	const Real lane = (Real)((wave % 3) - 1) * half * 0.45f;	// left, middle, right
	for (int s = 0; s < 2; ++s) {
		const Real dir = s == 0 ? -1.0f : 1.0f;
		const Real face = s == 0 ? 0.0f : (Real)M_PI;
		AIGroupPtr g = TheAI->createGroup();
		for (int i = 0; i < perWave; ++i) {
			const int n = wave * perWave + i, r = i / cols, c = i % cols;
			Object *o = spawn(B.units[n % B.units.size()], B.side[s], B.cx + dir * (half * 0.3f + r * 20), B.cy + lane + (c - cols / 2) * 20, face);
			if (!o) continue;
			B.spawned[s]++;
			if (o->getAIUpdateInterface()) g->add(o);
		}
		Coord3D target = B.baseCenter[1 - s]; target.y += lane;
		g->groupAttackMoveToPosition(&target, NO_MAX_SHOTS_LIMIT, CMD_FROM_SCRIPT);
	}
}

// Where the other side's army is now: the middle of its living units.
Coord3D armyCenter(int s)
{
	Real x = 0, y = 0; int n = 0;
	for (Object *o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
		if (o->getControllingPlayer() == B.side[s] && !o->isKindOf(KINDOF_STRUCTURE) && !o->isEffectivelyDead()) { x += o->getPosition()->x; y += o->getPosition()->y; n++; }
	return n ? groundAt(x / n, y / n) : B.baseCenter[s];
}

// The player's camera: scroll along the front, visit both bases, turn. Positions are
// offsets from the map middle as fractions of half the map; one key every 5 s.
void moveCamera(UnsignedInt f)
{
	static const float keys[][3] = {	// x, y, angle (degrees from default)
		{ 0.0f, -0.5f, 0 }, { 0.0f, 0.5f, 0 }, { -0.6f, 0.0f, 0 }, { -0.2f, 0.3f, 0 },
		{ 0.1f, -0.4f, 0 }, { 0.1f, 0.4f, 30 }, { 0.0f, 0.0f, 30 }, { 0.6f, 0.0f, 0 },
		{ 0.2f, -0.3f, 0 }, { 0.0f, 0.4f, -20 }, { -0.3f, 0.0f, 0 }, { 0.0f, 0.0f, 0 }, { 0.0f, 0.0f, 0 } };
	const int nKeys = sizeof(keys) / sizeof(keys[0]);
	const float k = f / 150.0f;
	const int a = std::min((int)k, nKeys - 1), b = std::min(a + 1, nKeys - 1);
	const float t = k - (int)k;
	Region3D ext; TheTerrainLogic->getExtent(&ext);
	const Real half = std::min(ext.hi.x - ext.lo.x, ext.hi.y - ext.lo.y) / 2;
	const Real x = B.cx + half * (keys[a][0] + (keys[b][0] - keys[a][0]) * t);
	const Real y = B.cy + half * (keys[a][1] + (keys[b][1] - keys[a][1]) * t);
	Coord3D p = groundAt(x, y);
	TheTacticalView->lookAt(&p);
	TheTacticalView->setAngle((Real)((keys[a][2] + (keys[b][2] - keys[a][2]) * t) * M_PI / 180.0));
}

// The player's selection, changed every 2 s like a player managing a battle: another group,
// another target each time (deterministic: ties broken by object ID).
void selectAndOrder(int turn)
{
	Coord3D c; TheTacticalView->getPosition(&c);
	Region3D ext; TheTerrainLogic->getExtent(&ext);
	const Real half = std::min(ext.hi.x - ext.lo.x, ext.hi.y - ext.lo.y) / 2;
	const int kind = turn % 5;
	std::vector<std::pair<Real, Object *>> pick;
	for (Object *o = TheGameLogic->getFirstObject(); o; o = o->getNextObject()) {
		if (o->getControllingPlayer() != B.side[0] || o->isKindOf(KINDOF_STRUCTURE) || o->isEffectivelyDead() || !o->getDrawable() || !o->getAIUpdateInterface()) continue;
		const Real dx = o->getPosition()->x - c.x, dy = o->getPosition()->y - c.y, d2 = dx * dx + dy * dy;
		if (kind == 1 && !o->isKindOf(KINDOF_AIRCRAFT)) continue;
		if (kind == 2 && (!o->isKindOf(KINDOF_INFANTRY) || o->getPosition()->y > B.cy - half * 0.2f)) continue;
		if (kind == 4 && (!o->isKindOf(KINDOF_VEHICLE) || d2 > 600.0f * 600.0f)) continue;
		pick.push_back({ d2, o });
	}
	std::sort(pick.begin(), pick.end(), [](const std::pair<Real, Object *> &a, const std::pair<Real, Object *> &b) { return a.first < b.first || (a.first == b.first && a.second->getID() < b.second->getID()); });
	const size_t cap = kind == 3 ? 1 : 150;
	if (pick.size() > cap) pick.resize(cap);
	TheInGameUI->deselectAllDrawables(FALSE);
	if (pick.empty()) return;
	AIGroupPtr g = TheAI->createGroup();
	for (auto &e : pick) { TheInGameUI->selectDrawable(e.second->getDrawable()); g->add(e.second); }
	Coord3D target;
	switch (kind) {
	case 0: target = B.baseCenter[1]; break;										// near the camera: at the enemy base
	case 1: {																					// aircraft: at an enemy structure
		target = B.baseCenter[1];
		int n = 0;
		for (Object *o = TheGameLogic->getFirstObject(); o; o = o->getNextObject())
			if (o->getControllingPlayer() == B.side[1] && o->isKindOf(KINDOF_STRUCTURE) && !o->isEffectivelyDead() && n++ == turn * 7 % 50) { target = *o->getPosition(); break; }
		break; }
	case 2: target = groundAt(B.cx, B.cy); g->groupMoveToPosition(&target, FALSE, CMD_FROM_PLAYER); return;	// a front's infantry: to the middle
	case 3: target = armyCenter(1); break;										// one unit: at the enemy army
	default: target = groundAt(B.cx + half * 0.3f, B.cy + half * 0.4f * ((turn & 1) ? 1 : -1)); break;	// vehicles: another front
	}
	g->groupAttackMoveToPosition(&target, NO_MAX_SHOTS_LIMIT, CMD_FROM_PLAYER);
}

void fireNextStrike()
{
	while (B.nextStrike < B.strikes.size()) {
		const Strike k = B.strikes[B.nextStrike++];
		Object *o = TheGameLogic->findObjectByID(k.src);
		if (!o) continue;
		SpecialPowerModuleInterface *sp = o->getSpecialPowerModule(k.power);
		if (!sp) continue;
		sp->setReadyFrame(TheGameLogic->getFrame());
		Coord3D at = (B.fired / 2) % 2 ? B.baseCenter[1 - k.side] : armyCenter(1 - k.side);
		at.x += (Real)((B.fired * 37) % 160 - 80);	// spread them over the army
		at.y += (Real)((B.fired * 53) % 240 - 120);
		sp->doSpecialPowerAtLocation(&at, INVALID_ANGLE, COMMAND_FIRED_BY_SCRIPT);
		B.fired++;
		return;
	}
}

// ---------------------------------------------------------------------------- recording

void beginPhase(const char *name)
{
	B.phases.push_back(Phase());
	B.cur = &B.phases.back();
	B.cur->name = name;
	B.cur->wall0 = now();
	B.cur->logic0 = TheGameLogic ? TheGameLogic->getFrame() : 0;
}

void endPhase()
{
	if (!B.cur) return;
	B.cur->wall1 = now();
	B.cur->logic1 = TheGameLogic ? TheGameLogic->getFrame() : 0;
	B.cur = nullptr;
}

// avg, p50, p90, max of one value over the phase's frames
template <typename F> std::string dist(const std::vector<Sample> &v, F get, double scale = 1.0)
{
	std::vector<double> x; double sum = 0;
	for (const Sample &s : v) { double a = get(s) * scale; x.push_back(a); sum += a; }
	if (x.empty()) return "{\"avg\":0,\"p50\":0,\"p90\":0,\"max\":0}";
	std::sort(x.begin(), x.end());
	char buf[160];
	snprintf(buf, sizeof(buf), "{\"avg\":%.2f,\"p50\":%.2f,\"p90\":%.2f,\"max\":%.2f}", sum / x.size(), x[x.size() / 2], x[std::min(x.size() - 1, x.size() * 9 / 10)], x.back());
	return buf;
}

std::string phaseJson(const Phase &p)
{
	std::vector<float> ms; double drawSum = 0, uploadSum = 0, msSum = 0;
	int over50 = 0, over100 = 0;
	for (const Sample &s : p.s) { ms.push_back(s.ms); drawSum += s.draws; uploadSum += s.upload; msSum += s.ms; over50 += s.ms > 50; over100 += s.ms > 100; }
	std::sort(ms.begin(), ms.end());
	auto pct = [&](double q) { return ms.empty() ? 0.0 : (double)ms[std::min(ms.size() - 1, (size_t)(q * ms.size()))]; };
	const double secs = (p.wall1 - p.wall0) / 1000.0, n = std::max<size_t>(1, ms.size());
	double var = 0; for (float m : ms) var += (m - msSum / n) * (m - msSum / n);
	// 1% low: the frame rate of the slowest 1% of frames
	double worst = 0; size_t nWorst = std::max<size_t>(1, ms.size() / 100);
	for (size_t i = 0; i < nWorst && i < ms.size(); ++i) worst += ms[ms.size() - 1 - i];
	float heapAvg = 0, heapMax = 0, topMax = 0, objAvg = 0, objMax = 0, partAvg = 0, partMax = 0;
	for (const Slow &w : p.slow) { heapAvg += w.heapMB; heapMax = std::max(heapMax, w.heapMB); topMax = std::max(topMax, w.heapTopMB);
		objAvg += w.objects; objMax = std::max(objMax, (float)w.objects); partAvg += w.particles; partMax = std::max(partMax, (float)w.particles); }
	const float ns = (float)std::max<size_t>(1, p.slow.size());
	std::string j;
	char buf[900];
	snprintf(buf, sizeof(buf),
		"{\"name\":\"%s\",\"frames\":%d,\"seconds\":%.1f,\"fps\":%.1f,\"low1\":%.1f,\"p50ms\":%.1f,\"p90ms\":%.1f,\"p99ms\":%.1f,\"maxms\":%.1f,\"sdms\":%.1f,"
		"\"over50ms\":%d,\"over100ms\":%d,\"draws\":%.0f,\"uploadMBps\":%.2f,\"logicFrames\":%u,\"logicFps\":%.1f,"
		"\"heapMB\":%.0f,\"heapMaxMB\":%.0f,\"heapTopMB\":%.0f,\"objects\":%.0f,\"objectsMax\":%.0f,\"particles\":%.0f,\"particlesMax\":%.0f,\"note\":\"%s\"",
		p.name.c_str(), (int)ms.size(), secs, secs > 0 ? ms.size() / secs : 0.0,
		ms.empty() ? 0.0 : 1000.0 / (worst / std::min(nWorst, ms.size())), pct(0.5), pct(0.9), pct(0.99), ms.empty() ? 0.0 : (double)ms.back(), sqrt(var / n),
		over50, over100, ms.empty() ? 0.0 : drawSum / ms.size(), secs > 0 ? uploadSum / 1048576.0 / secs : 0.0, p.logic1 - p.logic0, secs > 0 ? (p.logic1 - p.logic0) / secs : 0.0,
		heapAvg / ns, heapMax, topMax, objAvg / ns, objMax, partAvg / ns, partMax, p.note.c_str());
	j = buf;
	j += ",\"frameMs\":" + dist(p.s, [](const Sample &s) { return (double)s.ms; });
	j += ",\"workMs\":" + dist(p.s, [](const Sample &s) { return (double)s.work; });
	j += ",\"engineDraws\":" + dist(p.s, [](const Sample &s) { return (double)s.draws; });
	j += ",\"glDraws\":" + dist(p.s, [](const Sample &s) { return (double)s.glDraws; });
	j += ",\"triangles\":" + dist(p.s, [](const Sample &s) { return (double)s.tris; });
	j += ",\"uploadKB\":" + dist(p.s, [](const Sample &s) { return (double)s.upload; }, 1.0 / 1024);
	j += ",\"programSwitches\":" + dist(p.s, [](const Sample &s) { return (double)s.programs; });
	j += ",\"textureBinds\":" + dist(p.s, [](const Sample &s) { return (double)s.binds; });
	j += "}";
	return j;
}

void finish(const char *why)
{
	endPhase();
	std::string j = "{\"version\":2,\"result\":\"";
	j += why;
	char buf[300];
	snprintf(buf, sizeof(buf), "\",\"units\":[%d,%d],\"structures\":[%d,%d],\"unitTypes\":%d,\"structureTypes\":%d,\"strikes\":%d,\"setupMs\":%.0f,\"setupNote\":\"%s\",\"phases\":[",
		B.spawned[0], B.spawned[1], B.built[0], B.built[1], B.unitTypes, B.structureTypes, B.fired, B.setupMs, B.setupNote.c_str());
	j += buf;
	for (size_t i = 0; i < B.phases.size(); ++i) { if (i) j += ","; j += phaseJson(B.phases[i]); }
	j += "]}";
	EM_ASM({ if (window.gxBenchDone) window.gxBenchDone(UTF8ToString($0)); }, j.c_str());
	B.stage = IDLE;
	B.phases.clear();
	if (TheGameLogic->isInGame() && !TheGameLogic->isInShellGame()) TheGameLogic->exitGame();
}

void startTourMap()
{
	while (B.tourIndex < kTourCount) {
		const char *name = kTourMaps[B.tourIndex];
		char text[120]; snprintf(text, sizeof(text), "Map %d of %d: %s", B.tourIndex + 1, kTourCount, name);
		progress(text);
		if (startSkirmish(name, false)) { B.stage = TOUR_LOAD; B.stageStart = now(); return; }
		beginPhase(name); B.cur->note = "map not found"; endPhase();
		B.tourIndex++;
	}
	finish("done");
}

// ---------------------------------------------------------------------------- per frame

double lastT = 0;

void onFrame(double workMs)
{
	if (B.stage == IDLE) return;
	// the main loop clamps its own frame time at 100 ms; slow frames are what this measures
	const double tNow = now();
	const double frameMs = lastT > 0 ? tNow - lastT : 16.7;
	lastT = tNow;
	Sample smp;
	smp.ms = (float)frameMs; smp.work = (float)workMs;
	smp.draws = gx_dx8_draw_called - B.lastDraws; B.lastDraws = gx_dx8_draw_called;
	smp.glDraws = gx_stat_gl_draws - B.lastGlDraws; B.lastGlDraws = gx_stat_gl_draws;
	smp.programs = gx_stat_programs - B.lastPrograms; B.lastPrograms = gx_stat_programs;
	smp.binds = gx_stat_tex_binds - B.lastBinds; B.lastBinds = gx_stat_tex_binds;
	smp.tris = (float)(gx_stat_triangles - B.lastTris); B.lastTris = gx_stat_triangles;
	smp.upload = (float)(gx_stat_upload_bytes - B.lastUpload); B.lastUpload = gx_stat_upload_bytes;
	if (B.cur && (B.stage == BATTLE_RUN || B.stage == TOUR_RUN)) {
		B.cur->s.push_back(smp);
		if (B.cur->s.size() % 30 == 1) {
			struct mallinfo mi = mallinfo();
			B.cur->slow.push_back({ (float)(mi.uordblks / 1048576.0), (float)((uintptr_t)sbrk(0) / 1048576.0),
				TheGameLogic ? (int)TheGameLogic->getObjectCount() : 0, TheParticleSystemManager ? (int)TheParticleSystemManager->getParticleCount() : 0 });
		}
	}
	const double t = now();

	switch (B.stage) {
	case BATTLE_LOAD:
		if (gameReady()) {
			progress("Battle: building both bases");
			const double s0 = now();
			setupBattle();
			B.setupMs = now() - s0;
			lastT = now();	// the setup's own time is reported apart, not as a frame
			beginPhase("armies");
			B.frame0 = TheGameLogic->getFrame();
			B.stage = BATTLE_RUN; B.stageStart = t;
			progress("Battle: armies arriving in waves");
		} else if (t - B.stageStart > kLoadWallMs) {
			beginPhase("battle"); B.cur->note = "map did not load"; finish("battle map did not load");
		}
		break;
	case BATTLE_RUN: {
		const UnsignedInt f = TheGameLogic->getFrame() - B.frame0;
		if (B.wavesSent < kWaves && f >= (UnsignedInt)B.wavesSent * kWaveEvery) sendWave(B.wavesSent++);
		moveCamera(f);
		if (f >= 30 && f / 60 != B.lastOrder) { B.lastOrder = f / 60; selectAndOrder((int)B.lastOrder); }
		if (B.cur && B.cur->name == "armies" && f >= kStrikesFrom) {
			endPhase(); beginPhase("strikes"); progress("Battle: superweapons and strikes");
		}
		if (f >= kStrikesFrom && (f - kStrikesFrom) / kStrikeEvery >= (UnsignedInt)B.fired && B.nextStrike < B.strikes.size()) fireNextStrike();
		if (B.cur && B.cur->name == "strikes" && B.nextStrike >= B.strikes.size()) {
			endPhase(); beginPhase("aftermath"); progress("Battle: aftermath");
		}
		if (f >= kBattleFrames || t - B.stageStart > kBattleWallMs) {
			if (t - B.stageStart > kBattleWallMs && B.cur) B.cur->note = "stopped at the time limit";
			endPhase();
			TheInGameUI->deselectAllDrawables(FALSE);
			TheTacticalView->setAngleToDefault();
			if (B.withTour) { B.tourIndex = 0; startTourMap(); } else finish("done");
		}
		break;
	}
	case TOUR_LOAD:
		if (gameReady()) {
			if (ThePartitionManager && ThePlayerList->getLocalPlayer())
				ThePartitionManager->revealMapForPlayerPermanently(ThePlayerList->getLocalPlayer()->getPlayerIndex());
			beginPhase(kTourMaps[B.tourIndex]);
			B.stage = TOUR_RUN; B.stageStart = t;
		} else if (t - B.stageStart > kLoadWallMs) {
			beginPhase(kTourMaps[B.tourIndex]); B.cur->note = "did not load"; endPhase();
			B.tourIndex++; startTourMap();
		}
		break;
	case TOUR_RUN: {
		// four points of the map, one second each; a thumbnail at the first
		const double e = t - B.stageStart;
		const int point = (int)(e / 1000.0);
		static int lastPoint = -1;
		if (e < frameMs + 1) lastPoint = -1;
		if (point != lastPoint && point < 4) {
			lastPoint = point;
			Region3D ext; TheTerrainLogic->getExtent(&ext);
			Coord3D p;
			p.x = ext.lo.x + (ext.hi.x - ext.lo.x) * ((point & 1) ? 0.7f : 0.3f);
			p.y = ext.lo.y + (ext.hi.y - ext.lo.y) * ((point & 2) ? 0.7f : 0.3f);
			p.z = TheTerrainLogic->getGroundHeight(p.x, p.y);
			TheTacticalView->lookAt(&p);
		}
		if (point == 0 && e > 600 && B.shotFor != B.tourIndex) { B.shotFor = B.tourIndex; gx_bench_shot = 1; }	// one thumbnail per map
		if (e >= 4000) {
			lastPoint = -1;
			endPhase();
			B.tourIndex++;
			startTourMap();
		}
		break;
	}
	default: break;
	}
}

} // namespace

// Page entry points ---------------------------------------------------------------------------

extern "C" EMSCRIPTEN_KEEPALIVE void GX_Bench_Start(int withTour)
{
#ifdef __EMSCRIPTEN_PTHREADS__
	extern pthread_t GX_GameThread;
	if (GX_GameThread && !pthread_equal(pthread_self(), GX_GameThread)) {
		emscripten_dispatch_to_thread_async(GX_GameThread, EM_FUNC_SIG_VI, (void *)GX_Bench_Start, nullptr, withTour);
		return;
	}
#endif
	if (B.stage != IDLE || !TheGameLogic) return;
	if (TheGameLogic->isInMultiplayerGame()) {
		EM_ASM({ if (window.gxBenchDone) window.gxBenchDone(JSON.stringify({ result: 'Not available during a network game' })); });
		return;
	}
	B = Bench();
	lastT = 0;
	B.withTour = withTour != 0;
	B.lastDraws = gx_dx8_draw_called;
	gx_frame_hook = onFrame;
	progress("Battle: loading Tournament Desert");
	if (!startSkirmish(kBattleMap, true)) {
		beginPhase("battle"); B.cur->note = "Tournament Desert not found"; finish("battle map not found");
		return;
	}
	B.stage = BATTLE_LOAD;
	B.stageStart = now();
}

extern "C" EMSCRIPTEN_KEEPALIVE void GX_Bench_Stop()
{
#ifdef __EMSCRIPTEN_PTHREADS__
	extern pthread_t GX_GameThread;
	if (GX_GameThread && !pthread_equal(pthread_self(), GX_GameThread)) {
		emscripten_dispatch_to_thread_async(GX_GameThread, EM_FUNC_SIG_V, (void *)GX_Bench_Stop, nullptr);
		return;
	}
#endif
	if (B.stage != IDLE) finish("stopped");
}
