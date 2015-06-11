#ifndef __CFG_UTIL_H__
#define __CFG_UTIL_H__

#include "cfgclient.h"
#include "cfg_struct.h"
#include "stdbool.h"

const char* state2String(int state);
const char* type2String(int state);
char **strsplit(const char* str, const char* delim, size_t* numtokens);
int get_partition_name(enum CFG_PARTITION partition, char *partion_name, char *path_prefix);
int parse_bootstrap_config(const char *buffer, bootstrap_config_t **bootstrap_config_array, int *size);

bool bootstrap_config_exist(const char *key, bootstrap_config_t **bootstrap_config_array, int size, char *host);

char * fullpath(char *fullpath, const char *key, const char *path_prefix);
int parsekey(char *key, const char *fullpath, const char *path_prefix);

#endif
