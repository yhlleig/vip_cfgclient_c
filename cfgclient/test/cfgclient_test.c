#include "cfgclient.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char *optarg;
extern int optind, opterr, optopt;
int prompt()
{
	printf(">>>> ");
	return 0;
}
int usage(int argc, char **argv)
{
	printf("Welcome to use config center client tester\n"
		   "Usage: %s [OPTION] \n"
		   "\t[-d] \tDomain, 1 thirdparty, 2 vms, 3 resource, default 1\n"
		   "\t[-a] \tAction, to instantly run once. option: get or watch, default to run interactively \n"
		   "\t[-k] \tKey,  key to get or watch, needed by action \n"
		   "\t[-h] \tHelp,  print help tips \n"
		   "\t[-l] \tLoglevel,  1 error, 2 warn, 3 info, 4 debug, default 4 \n"
		   , argv[0]
		);

	return 0;
}
extern void cfg_dump(cfgclient_t *cc, char *s);

void watcher(cfgclient_t *cc, const char *key,
        const char *new_value, int new_value_len, 
        void *watcherCtx)
{
	printf("I hava received a notification of key[%s]\n"
			"new_value=%s, new_value_len=%d \n" 
			, key, new_value, new_value_len);
	prompt();
}

int do_get(cfgclient_t *cc, const char *key)
{
	char value[1024] = {0};
	int value_len = sizeof(value);
	int rc = cfg_get_data(cc, key, value, &value_len);
	if (0 != rc)
	{
		printf("Call cfg_get_data fail, return %d \n", rc);
		return -1;
	}
	printf("Call cfg_get_data for key [%s] success \n", key);
	printf("value=%s\n", value);
	printf("value_len=%d\n", value_len);
	return 0;
}

int do_watch(cfgclient_t *cc, const char *key)
{
	char value[1024] = {0};
	int value_len = sizeof(value);
	int rc = cfg_set_watcher(cc, key, watcher, NULL, value, &value_len);
	if (0 != rc)
	{
		printf("Call cfg_set_watcher fail, return %d \n", rc);
		return -1;
	}
	printf("Call cfg_set_watcher for key [%s] success \n", key);
	printf("value=%s\n", value);
	printf("value_len=%d\n", value_len);
	return 0;
}

int do_unwatch(cfgclient_t *cc, const char *key)
{
	int rc = cfg_clear_watcher(cc, key);
	if (0 != rc)
	{
		printf("Call cfg_clear_watcher fail, return %d \n", rc);
		return -1;
	}
	printf("Call cfg_clear_watcher for key [%s] success \n", key);
	return 0;
}

int main(int argc, char **argv)
{
	int opt;
	int domain = 1;
	char action[32] = {0};
	char key[128] = {0};
	enum CFG_LOG_LEVEL loglevel = CFG_LOG_LEVEL_DEBUG;

	while ((opt = getopt(argc, argv, "d:a:k:l:h")) != -1)
	{
		switch (opt) 
		{
		case 'd':
			domain = atoi(optarg);
			break;
		case 'a':
			strcpy(action, optarg);
			break;
		case 'k':
			strcpy(key, optarg);
			break;
		case 'h':
			return usage(argc, argv);
		case 'l':
			loglevel = atoi(optarg);
		}
	}

	cfg_set_log(loglevel, 0, 0);
	cfgclient_t *cc = cfg_init(domain);
	if (!cc)
	{
		fprintf(stderr,"cfg_init return null\n");
		return 0;
	}
	if (strcmp(action,"get") == 0)
	{
		return do_get(cc, key);
	}
	if (strcmp(action,"watch") == 0)
	{
		return do_watch(cc, key);
	}

	char buf[2048];
	int rc = 0;
	int run = 1;
	prompt();
	while (run && fgets(buf, sizeof(buf), stdin)) 
	{
		size_t len = strlen(buf);
		if (buf[len-1] == '\n')	buf[--len] = '\0';
		if (strncmp(buf, "get", 3) == 0)
		{
			rc = do_get(cc, buf+4);
		}
		else if (strncmp(buf, "watch", 5) == 0)
		{
			rc = do_watch(cc, buf+6);
		}
		else if (strncmp(buf, "unwatch", 7) == 0)
		{
			rc = do_unwatch(cc, buf+8);
		}
		else if (strncmp(buf, "dump", 4) == 0)
		{
			char s[2048] = {0};
			cfg_dump(cc, s);
			printf("\n++++++++DUMP++++++++ \n%s\n++++++DUMP END++++++\n", s);
		}
		else if (strncmp(buf, "help", 4) == 0)
		{
			char s[2048] = {0};
			sprintf(s, 
				"get <key>  	--Get the value of specific key\n"
				"watch <key>  	--Watch the value change of specific key\n"
				"unwatch <key>  --Clear watch on the specific key\n"
				"dump   		--Dump memory state of this client handle");
			printf("\n========Usage======== \n%s\n=====================\n", s);
		}
		else if (strncmp(buf, "quit", 4) == 0)
		{
			run = 0;
			continue;
		}
		prompt();
	}

	printf("Quit in 3 seconds...\n");
	cfg_destroy(cc);
	sleep(3);

	return 0;
}
