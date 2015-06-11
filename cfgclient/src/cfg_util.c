#include "cfg_util.h"
#include "zookeeper.h"
#include "stdio.h"
#include "string.h"

const char* state2String(int state)
{
  if (state == 0)
    return "CLOSED_STATE";
  if (state == ZOO_CONNECTING_STATE)
    return "CONNECTING_STATE";
  if (state == ZOO_ASSOCIATING_STATE)
    return "ASSOCIATING_STATE";
  if (state == ZOO_CONNECTED_STATE)
    return "CONNECTED_STATE";
  if (state == ZOO_EXPIRED_SESSION_STATE)
    return "EXPIRED_SESSION_STATE";
  if (state == ZOO_AUTH_FAILED_STATE)
    return "AUTH_FAILED_STATE";

  return "INVALID_STATE";
}

const char* type2String(int state)
{
  if (state == ZOO_CREATED_EVENT)
    return "CREATED_EVENT";
  if (state == ZOO_DELETED_EVENT)
    return "DELETED_EVENT";
  if (state == ZOO_CHANGED_EVENT)
    return "CHANGED_EVENT";
  if (state == ZOO_CHILD_EVENT)
    return "CHILD_EVENT";
  if (state == ZOO_SESSION_EVENT)
    return "SESSION_EVENT";
  if (state == ZOO_NOTWATCHING_EVENT)
    return "NOTWATCHING_EVENT";

  return "UNKNOWN_EVENT_TYPE";
}

char **strsplit(const char* str, const char* delim, size_t* numtokens) 
{
    // copy the original string so that we don't overwrite parts of it
    // (don't do this if you don't need to keep the old line,
    // as this is less efficient)
    char *s = strdup(str);

    // these three variables are part of a very common idiom to
    // implement a dynamically-growing array
    size_t tokens_alloc = 1;
    size_t tokens_used = 0;
    char **tokens = calloc(tokens_alloc, sizeof(char*));

    char *token, *strtok_ctx;
    for (token = strtok_r(s, delim, &strtok_ctx);
            token != NULL;
            token = strtok_r(NULL, delim, &strtok_ctx)) {
        // check if we need to allocate more space for tokens
        if (tokens_used == tokens_alloc) {
            tokens_alloc *= 2;
            tokens = realloc(tokens, tokens_alloc * sizeof(char*));
        }
        tokens[tokens_used++] = strdup(token);
    }

    // cleanup
    if (tokens_used == 0) {
        free(tokens);
        tokens = NULL;
    } else {
        tokens = realloc(tokens, tokens_used * sizeof(char*));
    }
    *numtokens = tokens_used;
    free(s);

    return tokens;
}

int get_partition_name(enum CFG_PARTITION partition, char *partion_name, char *path_prefix)
{
	char *name = NULL;
    char *prefix = NULL;
	switch(partition)
	{
	case THIRDPARTY :
		name = "thirdparty";
        prefix = "/default/thirdparty/";
		break;
	case VMS : 
		name = "vms";
        prefix = "/default/vms/";
		break;
	case RESOURCE : 
		name = "resource";
        prefix = "/default/resource/";
		break;
	case ARTIFACT : 
		name = "artifact";
        prefix = "/default/artifact/";
		break;
	case SERVICE:
		name = "service";
        prefix = "/default/service/";
		break;
	default :
		return -1;
	}
	strcpy(partion_name, name);
    strcpy(path_prefix, prefix);
	return 0;
}

int parse_bootstrap_config(const char *buffer, bootstrap_config_t **bootstrap_config_array, int *size)
{
	char **vec = NULL;
    size_t num;
    vec = strsplit(buffer, "\n", &num);
    int i = 0;
    int index = 0;
    for (; i < num; i++)
    {
        char *s = vec[i];
        if(!s) continue;
		
		char **kv = NULL;
    	size_t elemNum;
    	kv = strsplit(buffer, "=", &elemNum);
    	if ( !kv || elemNum != 2 || !(kv[0]) || !(kv[1]) )
    	{
    		continue;
    	}
		bootstrap_config_t *biz_cfg = calloc(1, sizeof(bootstrap_config_t));
		strcpy(biz_cfg->partition_name, kv[0]);
		strcpy(biz_cfg->zk_host, kv[1]);
		free(kv[0]);
		free(kv[1]);
		free(kv);

		bootstrap_config_array[index] = biz_cfg;
		index++;
        free(s);
    }
    free(vec);
    *size = index;
    return 0;
}

bool bootstrap_config_exist(const char *key, bootstrap_config_t **bootstrap_config_array, int size, char *host)
{
	int i = 0;
	bool find = false;
	for (;i<size;i++)
	{
		bootstrap_config_t *biz_cfg = bootstrap_config_array[i];
		if (strcmp(biz_cfg->partition_name, key) == 0)
		{
			find = true;
			strcpy(host, biz_cfg->zk_host);
			break;
		}
	}
	return find;
}

char * fullpath(char *fullpath, const char *key, const char *path_prefix)
{
    snprintf(fullpath, 64, "%s%s", path_prefix, key);
    return fullpath;
}

int parsekey(char *key, const char *fullpath, const char *path_prefix)
{
    if (strncmp(fullpath, path_prefix, strlen(path_prefix)) != 0)
    {
        return -1;
    }
    strcpy(key, fullpath+strlen(path_prefix));
    return 0;
}
