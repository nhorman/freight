/*********************************************************
 *Copyright (C) 2015 Neil Horman
 *This program is free software; you can redistribute it and\or modify
 *it under the terms of the GNU General Public License as published 
 *by the Free Software Foundation; either version 2 of the License,
 *or  any later version.
 *
 *This program is distributed in the hope that it will be useful,
 *but WITHOUT ANY WARRANTY; without even the implied warranty of
 *MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *GNU General Public License for more details.
 *
 *File: freightctl_main.c
 *
 *Author:Neil Horman
 *
 *Date: 4/9/2015
 *
 *Description
 *********************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <signal.h>
#include <config.h>
#include <freight-log.h>
#include <freight-config.h>
#include <freight-db.h>
#include <xmlrpc-c/abyss.h>

struct agent_config config;

static TServer abyssServer;

extern void handle_freight_rpc(TSession *sessionP, TRequestInfo *requestP);

/*
 * This....is a hot mess.  Its here because xmlrpc doesn't currently export RequestAuth, so
 * I have to duplicate it here, along with all its internals.  I know...I could just make my
 * own exported prototype to reference whats in the library, but the library version seems to 
 * have lots of bugs in it, so this, sadly, seems to be the saner approach for now
 */
#ifndef __cplusplus
/* At least the GNU compiler defines __bool_true_false_are_defined */
#ifndef __bool_true_false_are_defined
#define __bool_true_false_are_defined
typedef enum {
    false = 0,
    true = 1
} bool;
#endif
#endif

void
NextToken(const char ** const pP) {

	bool gotToken;

	gotToken = false;

	while (!gotToken) {
		switch (**pP) {
		case '\t':
		case ' ':
			++(*pP);
			break;
		default:
			gotToken = true; 
	};
	}
}

char *
GetToken(char ** const pP) {

	char * p0;

	p0 = *pP;

	while (1) {
		switch (**pP) {
	case '\t':
	case ' ':
	case CR:
	case LF:
	case '\0':
		if (p0 == *pP)
			return NULL;

		if (**pP) {
			**pP = '\0';
			++(*pP);
		};
		return p0;

	default:
		++(*pP);
	};
	}
}

void
GetTokenConst(char **       const pP,
              const char ** const tokenP) {

	*tokenP = GetToken(pP);
}

/* Conversion table. */
static char etbl[64] = {
	'A','B','C','D','E','F','G','H',
	'I','J','K','L','M','N','O','P',
	'Q','R','S','T','U','V','W','X',
	'Y','Z','a','b','c','d','e','f',
	'g','h','i','j','k','l','m','n',
	'o','p','q','r','s','t','u','v',
	'w','x','y','z','0','1','2','3',
	'4','5','6','7','8','9','+','/'
};

static char *dtbl = NULL;


void
xmlrpc_base64Encode(const char * const chars,
                    char *       const base64) {
	unsigned int i;
	uint32_t length;
	char * p;
	const char * s;

	length = strlen(chars);  /* initial value */
	s = &chars[0];  /* initial value */
	p = &base64[0];  /* initial value */
	/* Transform the 3x8 bits to 4x6 bits, as required by base64. */
	for (i = 0; i < length; i += 3) {
		*p++ = etbl[s[0] >> 2];
		*p++ = etbl[((s[0] & 3) << 4) + (s[1] >> 4)];
		*p++ = etbl[((s[1] & 0xf) << 2) + (s[2] >> 6)];
		*p++ = etbl[s[2] & 0x3f];
		s += 3;
	}

	/* Pad the result if necessary... */
	if (i == length + 1)
		*(p - 1) = '=';
	else if (i == length + 2)
		*(p - 1) = *(p - 2) = '=';

	/* ...and zero-terminate it. */
	*p = '\0';
}

void xmlrpc_base64Decode(const char *base64,
			 char *buf) {

	int input_length = strlen(base64);
	int output_length = input_length / 4 * 3;
	int i,j;

	if (!dtbl) {
		dtbl = malloc(256);

		for (i = 0; i < 64; i++)
			dtbl[(unsigned char) etbl[i]] = i;
	}

	if (base64[input_length - 1] == '=') (output_length)--;
	if (base64[input_length - 2] == '=') (output_length)--;

	for (i = 0, j = 0; i < input_length;) {

		uint32_t sextet_a = base64[i] == '=' ? 0 & i++ : dtbl[(int)base64[i++]];
		uint32_t sextet_b = base64[i] == '=' ? 0 & i++ : dtbl[(int)base64[i++]];
		uint32_t sextet_c = base64[i] == '=' ? 0 & i++ : dtbl[(int)base64[i++]];
		uint32_t sextet_d = base64[i] == '=' ? 0 & i++ : dtbl[(int)base64[i++]];

		uint32_t triple = (sextet_a << 3 * 6)
			+ (sextet_b << 2 * 6)
			+ (sextet_c << 1 * 6)
			+ (sextet_d << 0 * 6);

		buf[j++] = (triple >> 2 * 8) & 0xFF;
		buf[j++] = (triple >> 1 * 8) & 0xFF;
		buf[j++] = (triple >> 0 * 8) & 0xFF;
    }

}


bool
RequestAuthFromDb(TSession *   const sessionP,
            const char * const credential) {
	bool authorized;
	char * authHdrPtr;
	char *user, *pass, *dbpass;

	authHdrPtr = RequestHeaderValue(sessionP, "authorization");
	if (authHdrPtr) {
		const char * authType;
		authType = GetToken(&authHdrPtr);
		if (authType) {
			if (!strcasecmp(authType, "basic")) {
				char temp[512];
				NextToken((const char **)&authHdrPtr);
				memset(temp, 0, 512);
				xmlrpc_base64Decode(authHdrPtr, temp);
				user = temp;
				pass = strchr(temp, ':');
				*pass = 0;
				pass++;
				dbpass = get_tennant_proxy_pass(user, &config);
				if (!strcmp(dbpass, pass)) {
					authorized = TRUE;
				} else
					authorized = FALSE;
			} else
				authorized = FALSE;
		} else
			authorized = FALSE;
	} else
		authorized = FALSE;

	if (!authorized) {
		char hdrValue[1024];
		sprintf(hdrValue, "Basic realm=\"%s\"", credential);
		ResponseAddField(sessionP, "WWW-Authenticate", hdrValue);

		ResponseStatus(sessionP, 401);
	}
	return authorized;
}



#ifdef HAVE_GETOPT_LONG
struct option lopts[] = {
	{"help", 0, NULL, 'h'},
	{"config", 1, NULL, 'c'},
	{"verbose", 0, NULL, 'v'},
	{ 0, 0, 0, 0}
};
#endif

static void usage(char **argv)
{
#ifdef HAVE_GETOPT_LONG
	LOG(INFO, "%s [-h | --help] "
		"[-c | --config=<config>] <op>\n", argv[0]);
#else
	frpintf(stderr, "%s [-h] [-c <config>] <op>\n", argv[0];
#endif
}

static abyss_bool handleDefaultRequest(TSession * const sessionP)
{
	ResponseStatus(sessionP, 500); /* Not found */
	ResponseError2(sessionP, "We don't serve regular html in these here parts");
	return TRUE;
}

static void handleFreightRPC(void * const handler,
			     TSession * const sessionP,
			     abyss_bool * const handledP)
{
	abyss_bool authenticated;
	TRequestInfo *requestP = NULL;

	*handledP = FALSE;

	authenticated = RequestAuthFromDb(sessionP, "FreightProxyDomain");

	if (!authenticated)
		return;				   

	SessionGetRequestInfo(sessionP, (const TRequestInfo ** const)&requestP);

	if (!requestP){
		ResponseStatus(sessionP, 500);
		ResponseError2(sessionP, "Invalid request");
		return;
	}

	handle_freight_rpc(sessionP, requestP);

	*handledP = TRUE;

}


static void sigint_handler(int sig, siginfo_t *info, void *ptr)
{
	ServerTerminate(&abyssServer);
}

int main(int argc, char **argv)
{
	int rc = 1;
	int opt, longind;
	char *config_file = "/etc/freight-agent/config";
	int verbose = 0;
	abyss_bool arc;
	struct sigaction intact;
	const char *err = NULL;
	struct ServerReqHandler3 const hDesc = {
		NULL, /* term */
		handleFreightRPC, /* handleReq */
		NULL, /* Userdata */
		65536 /* Stack size */
	};

	/*
 	 * Parse command line options
 	 */

#ifdef HAVE_GETOPT_LONG
	while ((opt = getopt_long(argc, argv, "hc:", lopts, &longind)) != -1) {
#else
	while ((opt = getopt(argc, argv, "hc:") != -1) {
#endif
		switch(opt) {

		case '?':
			/* FALLTHROUGH */
		case 'h':
			usage(argv);
			goto out;
			/* NOTREACHED */
			break;
		case 'c':
			config_file = optarg;
			break;
		case 'v':
			verbose = 1;
			break;
		}
	}

	/*
	 * Read in the configuration file
	 */
	rc = read_configuration(config_file, &config);
	if (rc)
		goto out_release;

	config.cmdline.verbose = verbose;

	rc = -EINVAL;

	if (!get_db_api(&config)) {
		LOG(ERROR, "No DB configuration selected\n");
		goto out_release;
	}

	if (db_init(&config)) {
		LOG(ERROR, "Unable to initalize DB subsystem\n");
		goto out_release;
	}

	if (db_connect(&config)) {
		LOG(ERROR, "Failed to connect to database\n");
		goto out_cleanup_db;
	}
	

	arc  = ServerCreate(&abyssServer, "FreightProxyServer",
			   config.proxy.serverport, NULL, config.proxy.logpath);
	if (!arc) {
		LOG(ERROR, "Could not crate xmlrpc server: %s\n", strerror(errno));
		goto out_disconnect;
	}

	ServerInit2(&abyssServer, &err);
	if (err) {
		LOG(ERROR, "Unable to init xmlrpc server: %s\n", err);
		goto out_serverfree;
	}

	ServerDefaultHandler(&abyssServer, handleDefaultRequest);

	ServerAddHandler3(&abyssServer, &hDesc, &arc);

	if (!arc) {
		LOG(ERROR, "Could not add xmlrpc request handler\n");
		goto out_serverfree;
	}

	memset(&intact, 0, sizeof(struct sigaction));

	intact.sa_sigaction = sigint_handler;
	intact.sa_flags = SA_SIGINFO;
	sigaction(SIGINT, &intact, NULL);

	LOG(INFO, "Accpeting connections on xml rpc server on port %d\n", config.proxy.serverport);
	ServerRun(&abyssServer);
	rc = 0;

out_serverfree:
	ServerFree(&abyssServer);

out_disconnect:
	db_disconnect(&config);
out_cleanup_db:
	db_cleanup(&config);
out_release:
	release_configuration(&config);
out:
	return rc;
}
