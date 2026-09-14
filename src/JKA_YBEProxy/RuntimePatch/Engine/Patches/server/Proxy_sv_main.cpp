#include "JKA_YBEProxy/RuntimePatch/Engine/Proxy_Engine_Wrappers.hpp"
#include "JKA_YBEProxy/Imports/server/sv_game.hpp"
#include "JKA_YBEProxy/Proxy_Header.hpp"
#include "sdk/game/g_public.hpp"
#include "Proxy_sv_main.hpp"

#include <cstring>

/*
===================
YBE OOB rate limiter (works with YBE, base + JA+)

Stock engine has no throttling for connectionless packets, so spoofed
getstatus/getinfo/connect/rcon floods each cost a full handler run.
This is a small per-IP + global leaky bucket run inside YBE's own
SV_ConnectionlessPacket detour, before any handler runs.

- Engine packet handling is single-threaded, no locks needed.
- Loopback (local tools/rcon) is exempt.
- Fail closed on bad clock, fail open only when the server clock isn't up yet.
===================
*/

namespace
{
	struct OOBBucket_t
	{
		byte	ip[4];
		int		lastTime;
		int		burst;
		qboolean used;
	};

	constexpr int OOB_BUCKETS = 256;
	// Per-IP: 10/sec sustained, burst 10. Global: 100/sec sustained, burst 100.
	constexpr int OOB_IP_PERIOD = 100;
	constexpr int OOB_IP_BURST = 10;
	constexpr int OOB_GLOBAL_PERIOD = 10;
	constexpr int OOB_GLOBAL_BURST = 100;

	OOBBucket_t oobBuckets[OOB_BUCKETS] = {};
	int oobGlobalBurst = 0;
	int oobGlobalLast = 0;
	int oobDropped = 0;
	int oobLastLog = 0;

	bool OOB_RateLimit(int* burst, int* lastTime, int maxBurst, int period, int now)
	{
		int interval;
		int expired;

		if (!burst || !lastTime || period <= 0 || maxBurst <= 0)
			return false;

		if (now < *lastTime)
		{
			*burst = 0;
			*lastTime = now;
			return false;
		}

		interval = now - *lastTime;
		expired = interval / period;

		if (expired > *burst)
		{
			*burst = 0;
			*lastTime = now;
		}
		else
		{
			*burst -= expired;
			*lastTime = now - (interval % period);
		}

		if (*burst < maxBurst)
		{
			(*burst)++;
			return false;
		}

		return true;
	}

	OOBBucket_t* OOB_BucketFor(const byte ip[4])
	{
		int victim = 0;
		int oldest = 0;
		bool foundFree = false;

		for (int i = 0; i < OOB_BUCKETS; i++)
		{
			if (oobBuckets[i].used)
			{
				if (!std::memcmp(oobBuckets[i].ip, ip, 4))
					return &oobBuckets[i];

				if (oobBuckets[i].lastTime < oobBuckets[oldest].lastTime)
					oldest = i;
			}
			else if (!foundFree)
			{
				victim = i;
				foundFree = true;
			}
		}

		if (!foundFree)
			victim = oldest;

		std::memset(&oobBuckets[victim], 0, sizeof(oobBuckets[victim]));
		std::memcpy(oobBuckets[victim].ip, ip, 4);
		oobBuckets[victim].used = qtrue;

		return &oobBuckets[victim];
	}

	bool OOB_ShouldDrop(const netadr_t from, int now)
	{
		OOBBucket_t* bucket;

		if (from.type == NA_LOOPBACK)
			return false;

		if (from.type != NA_IP)
			return false;

		// Local tools exemption (127.0.0.1).
		if (from.ip[0] == 127 && from.ip[1] == 0 && from.ip[2] == 0 && from.ip[3] == 1)
			return false;

		bucket = OOB_BucketFor(from.ip);

		if (OOB_RateLimit(&bucket->burst, &bucket->lastTime, OOB_IP_BURST, OOB_IP_PERIOD, now))
			return true;

		if (OOB_RateLimit(&oobGlobalBurst, &oobGlobalLast, OOB_GLOBAL_BURST, OOB_GLOBAL_PERIOD, now))
			return true;

		return false;
	}
}

/*
===================
SV_CalcPings

Updates the cl->ping variables
===================
*/

void (*Original_SV_CalcPings)(void);
void Proxy_SV_CalcPings(void)
{
	int			i, j;
	client_t* cl;
	int			total, count;
	int			delta;
	playerState_t* ps;

	for (i = 0; i < server.cvars.sv_maxclients->integer; i++)
	{
		cl = &server.svs->clients[i];
		if (cl->state != CS_ACTIVE)
		{
			cl->ping = 999;
			continue;
		}
		if (!cl->gentity)
		{
			cl->ping = 999;
			continue;
		}
		if (cl->gentity->r.svFlags & SVF_BOT)
		{
			cl->ping = 0;
			continue;
		}

		total = 0;
		count = 0;

		for (j = 0; j < PACKET_BACKUP; j++)
		{
			// Proxy -------------->
			// if (cl->frames[j].messageAcked <= 0)
			if (cl->frames[j].messageAcked == -1)
			// Proxy <--------------
			{
				continue;
			}
			delta = cl->frames[j].messageAcked - cl->frames[j].messageSent;
			count++;
			total += delta;
		}
		if (!count)
		{
			cl->ping = 999;
		}
		else
		{
			cl->ping = total / count;
			
			if (cl->ping > 999)
			{
				cl->ping = 999;
			}

			// Proxy -------------->
			if (proxy.originalEngineCvars.proxy_sv_pingFix.integer && cl->ping < 1)
			{
				cl->ping = 1;
			}
			// Proxy <--------------
		}

		// let the game dll know about the ping
		// Proxy -------------->
		// ps = SV_GameClientNum( i );
		ps = Proxy_GetPlayerStateByClientNum(i);
		// Proxy <--------------
		ps->ping = cl->ping;
	}
}

void (*Original_SVC_Status)(netadr_t);
void Proxy_SVC_Status(netadr_t from) {
	// Proxy -------------->
	// Fix q3infoboom
	// In Info_SetValueForKey if the infostring is too big then it will drop the server
	if (std::strlen(server.common.functions.Cmd_Argv(1)) > 128)
	{
		return;
	}
	// Proxy <--------------

	Original_SVC_Status(from);
}

void (*Original_SVC_Info)(netadr_t);
void Proxy_SVC_Info(netadr_t from) {
	// Proxy -------------->
	// Fix q3infoboom
	// In Info_SetValueForKey if the infostring is too big then it will drop the server
	if (std::strlen(server.common.functions.Cmd_Argv(1)) > 128)
	{
		return;
	}
	// Proxy <--------------

	Original_SVC_Info(from);
}

/*
===============
SVC_RemoteCommand

An rcon packet arrived from the network.
Shift down the remaining args
Redirect all printfs
===============
*/
void (*Original_SVC_RemoteCommand)(netadr_t, msg_t*);
void Proxy_SVC_RemoteCommand(netadr_t from, msg_t* msg) {
	static unsigned int commandCoolDown = 0;

	// JA+ admin tools send rapid rcon; YBE's global cooldown breaks them. Skip it for JA+.
	if (!proxy.isJAPlus && proxy.originalEngineCvars.proxy_sv_enableRconCmdCooldown.integer && (unsigned int)server.svs->time < commandCoolDown + 500) {
		return;
	}

	commandCoolDown = (unsigned int)server.svs->time;
	
	// Only allow writeconfig on cfg files
	if (!Q_stricmpn(server.common.functions.Cmd_Argv(2), "writeconfig", 11)) {
		const char* arg3 = server.common.functions.Cmd_Argv(3);
		const std::size_t arg3Len = std::strlen(arg3);
		
		if (arg3Len >= 5 && arg3[arg3Len - 4] == '.' && Q_stricmpn(&arg3[arg3Len - 4], ".cfg", 4)) {
			return;
		}
	}

	Original_SVC_RemoteCommand(from, msg);
}

/*
=================
SV_ConnectionlessPacket

A connectionless packet has four leading 0xff
characters to distinguish it from a game channel.
Clients that are in the game can still send
connectionless packets.
=================
*/
void (*Original_SV_ConnectionlessPacket)(netadr_t, msg_t*);
void Proxy_SV_ConnectionlessPacket(netadr_t from, msg_t* msg) {
	char* s;
	char* c;
	int now;

	// YBE OOB bucket: drop floods before they cost a handler run. Same for base + JA+.
	// Fail open only while the server clock isn't up yet.
	if (server.svs)
	{
		now = server.svs->time;
		if (now != 0 && OOB_ShouldDrop(from, now))
		{
			oobDropped++;
			if (oobLastLog + 5000 < now)
			{
				server.common.functions.Com_Printf("OOB rate limit: dropped %d connectionless requests\n", oobDropped);
				oobLastLog = now;
				oobDropped = 0;
			}
			return;
		}
	}

	server.common.functions.MSG_BeginReadingOOB(msg);
	server.common.functions.MSG_ReadLong(msg);		// skip the -1 marker

	if (!Q_strncmp("connect", (const char*)&msg->data[4], 7)) {
		server.common.functions.Huff_Decompress(msg, 12);
	}

	s = server.common.functions.MSG_ReadStringLine(msg);
	server.common.functions.Cmd_TokenizeString(s);

	c = server.common.functions.Cmd_Argv(0);

	if (server.common.cvars.com_developer->integer) {
		server.common.functions.Com_Printf("SV packet %s : %s\n", server.common.functions.NET_AdrToString(from), c);
	}

	if (!Q_stricmp(c, "getstatus")) {
		server.functions.SVC_Status(from);
	}
	else if (!Q_stricmp(c, "getinfo")) {
		server.functions.SVC_Info(from);
	}
	else if (!Q_stricmp(c, "getchallenge")) {
		server.functions.SV_GetChallenge(from);
	}
	else if (!Q_stricmp(c, "connect")) {
		server.functions.SV_DirectConnect(from);
	}
	else if (!Q_stricmp(c, "ipAuthorize")) {
		//SV_AuthorizeIpPacket(from);
	}
	else if (!Q_stricmp(c, "rcon")) {
		server.functions.SVC_RemoteCommand(from, msg);
	}
	else if (!Q_stricmp(c, "disconnect")) {
		// if a client starts up a local server, we may see some spurious
		// server disconnect messages when their new server sees our final
		// sequenced messages to the old client
	}
	else {
		// Block default-deny: unknown OOB is dropped for base and JA+ alike.
		// Anything we can't explicitly handle/patch must not reach the engine.
		if (server.common.cvars.com_developer->integer) {
			server.common.functions.Com_Printf("bad connectionless packet from %s:\n%s\n",
				server.common.functions.NET_AdrToString(from), s);
		}
	}
}

/*
=================
SV_ReadPackets
=================
*/
void (*Original_SV_PacketEvent)(netadr_t, msg_t*);
void Proxy_SV_PacketEvent(netadr_t from, msg_t* msg) {
	int			i;
	client_t* cl;
	int			qport;

	// check for connectionless packet (0xffffffff) first
	if (msg->cursize >= 4 && *(int*)msg->data == -1) {
		server.functions.SV_ConnectionlessPacket(from, msg);
		return;
	}

	// read the qport out of the message so we can fix up
	// stupid address translating routers
	server.common.functions.MSG_BeginReadingOOB(msg);
	server.common.functions.MSG_ReadLong(msg);				// sequence number
	qport = server.common.functions.MSG_ReadShort(msg) & 0xffff;

	// find which client the message is from
	for (i = 0, cl = server.svs->clients; i < server.cvars.sv_maxclients->integer; i++, cl++) {
		if (cl->state == CS_FREE) {
			continue;
		}
		if (!server.common.functions.NET_CompareBaseAdr(from, cl->netchan.remoteAddress)) {
			continue;
		}
		// it is possible to have multiple clients from a single IP
		// address, so they are differentiated by the qport variable
		if (cl->netchan.qport != qport) {
			continue;
		}

		// the IP port can't be used to differentiate them, because
		// some address translating routers periodically change UDP
		// port assignments
		if (cl->netchan.remoteAddress.port != from.port) {
			server.common.functions.Com_Printf("SV_ReadPackets: fixing up a translated port\n");
			cl->netchan.remoteAddress.port = from.port;
		}

		// make sure it is a valid, in sequence packet
		if (server.functions.SV_Netchan_Process(cl, msg)) {
			// zombie clients still need to do the Netchan_Process
			// to make sure they don't need to retransmit the final
			// reliable message, but they don't do any other processing
			if (cl->state != CS_ZOMBIE) {
				cl->lastPacketTime = server.svs->time;	// don't timeout
				server.functions.SV_ExecuteClientMessage(cl, msg);
			}
		}
		return;
	}

	// if we received a sequenced packet from an address we don't recognize,
	// send an out of band disconnect packet to it
	server.common.functions.NET_OutOfBandPrint(NS_SERVER, from, "disconnect");
}
