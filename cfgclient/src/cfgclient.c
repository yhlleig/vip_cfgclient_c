#include "cfgclient.h"
#include "zookeeper.h"
#include "zookeeper_log.h"
#include "cfg_struct.h"
#include "cfg_util.h"
#include "cfg_keynode.h"
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#define VALID_CHECK(cc) \
do { \
	if(!cc) { \
		LOG_ERROR(("config client handle is null")); \
		return ERR_INVALID_HANDLE; \
	} if (cc->workstate != WORKING) { \
		LOG_ERROR(("config client is not avaliable now, try later...")); \
		return ERR_HANDLE_UNAVALIABLE; \
	} \
} while(0)

#define FREE_CONFIG_ARRAY \
do { \
	int i = 0; \
	for (; i<size;i++) \
	{ \
		if(bootstrap_config_array[i]) \
			free(bootstrap_config_array[i]); \
	} \
} while(0)

// 只用来观测zh与zk集群的变化，数据的变化走另外一个监控回调
void zh_watcher(zhandle_t *zzh, int type, int state, const char *path, void* context)
{
    LOG_INFO(("Watcher %s state = %s for path %s", 
    	type2String(type), state2String(state), (path && strlen(path) > 0)? path : "[EMPTY]" ));
    if (type == ZOO_SESSION_EVENT)
    {
		cfgclient_t *cc = (cfgclient_t *)context;
		// zh 重连成功后会有connected的状态
		if (state == ZOO_CONNECTED_STATE)
		{
			reload_zk(cc, 0);
		}
		// 若为session过期 则需要重新连接
		else if (state == ZOO_EXPIRED_SESSION_STATE || state == ZOO_AUTH_FAILED_STATE)
		{
			reload_zk(cc, 1);
		}
	}
	else 
	{
		LOG_INFO(("Nothing need to do in this condition"));
	}
}

extern void zoo_set_log_stream(FILE* stream);

int cfg_set_log(enum CFG_LOG_LEVEL loglevel, const char *logdir, const char *logfile)
{
	//LOG_INFO(("Set log info. loglevel=%d logdir=%s logfile=%s", loglevel, logdir, logfile));
    char slog[128] = {0};
    if (logdir)
    {
    	strcpy(slog, logdir);
    }
    else
    {
    	strcpy(slog, "./");
    }
    if ( mkdir( slog, 0777 ) < 0 ) {
        if ( errno != EEXIST ){
        	LOG_ERROR(("Mkdir for logdir error"));
        	return ERR_LOG_SET_FAIL;
        }
    }
    if (logfile)
    {
    	strcat(slog, logfile);
    }
    else
    {
    	strcat(slog, "cfgclient.log");
    }
    FILE * logStream = fopen(slog, "a+");
    if (!logStream)
    {
    	LOG_ERROR(("Init log stream error. input path [%s]", slog));
    	return ERR_LOG_SET_FAIL;
    }
    zoo_set_debug_level(loglevel); // 注意这里的CFG_LOG_LEVEL 与 ZOO_LOG_LEVEL 是对应的
    zoo_set_log_stream(logStream);
    return 0;
}

cfgclient_t * cfg_init(enum CFG_PARTITION partition)
{
	LOG_INFO(("Begin to init config client for partition %d", (int)partition));
	cfgclient_t *cc = NULL;
	int rc = 0;

    bootstrap_config_t *bootstrap_config_array[16] = {0};
    int size = 0;

	cc = calloc(1, sizeof(*cc));
	if(!cc)
	{
		LOG_ERROR(("Alloc memory for client handle error, return NULL"));
		return NULL;
	}
	memset(cc, 0, sizeof(cfgclient_t));
	cc->workstate = PENDING;
	// get partition name from input enum
	rc = get_partition_name(partition, cc->partition, cc->path_prefix);
    if (0 != rc) {
    	LOG_ERROR(("Input partition invalid. RETURN %d ", rc));
        goto quit;
    }
	// init zh
	zhandle_t *zh_bootstrap = zookeeper_init(DEFAULT_CONFIG_CENTER, NULL, 30000, 0, 0, 0);
	if (!zh_bootstrap) {
    	LOG_ERROR(("Init bootstrap zookeeper fail "));
        goto quit;
    }
    LOG_INFO(("Init bootstrap zk success"));

	char buffer[1024 * 2] = {0};
    int buffer_len = sizeof(buffer);
    rc = zoo_get(zh_bootstrap, "/default/bootstrap", 0, buffer, &buffer_len, NULL);
    if (0 != rc) {
    	LOG_ERROR(("Get /default/bootstrap fail. RETURN %d ", rc));
        goto quit;
    }
    LOG_INFO(("Get /default/bootstrap value success"));

    parse_bootstrap_config(buffer, bootstrap_config_array, &size);
    // bootstrap 中填写的 均为 /vms/**=xxx的形式，这里在array中存储完整的key，查询时需要拼装
    char partkey[64] = {0};
    sprintf(partkey, "/%s/**",cc->partition);
    if (bootstrap_config_exist(partkey, bootstrap_config_array, size, cc->zkhost))
    {
    	LOG_INFO(("domain %s is configured in bootstrap", partkey));
    	// init new zh. context is the cc pointer
    	int cnt = 0;
    	cc->zh = NULL;
		do {
			cc->zh = zookeeper_init(cc->zkhost, zh_watcher, 30000, 0, cc, 0);
			cnt++;
		} while (cc->zh == NULL && cnt < 3);

		if (cc->zh == NULL)
		{
			LOG_ERROR(("init zookeeper fail."));
			goto quit;
		}
    	// destroy bootstrap
    	if (zookeeper_close(zh_bootstrap) != 0)
    	{
    		LOG_ERROR(("Destroy zh_bootstrap fail."));
    	}
    }
    else // not exist
    {
    	LOG_INFO(("domain %s is *NOT* configured in bootstrap, use bootstrap as ZK Svr", partkey));
    	strcpy(cc->zkhost, DEFAULT_CONFIG_CENTER);
    	cc->zh = zh_bootstrap;
    	// 设置watcher
    	zoo_set_watcher(cc->zh, zh_watcher);
    	zoo_set_context(cc->zh, cc);
    	strcpy(cc->zkhost, DEFAULT_CONFIG_CENTER);
    	// bootstrap不用了
    	zh_bootstrap = NULL;
    }
    LOG_INFO(("Init locks begin"));
	cc->qhead = NULL;
	rc = pthread_rwlock_init(&cc->queue_rwlock, NULL);
	if (rc != 0)
	{
		LOG_ERROR(("Init queue_rwlock fail. RETURN %d", rc));
		goto quit;
	}
	rc = pthread_mutex_init(&cc->cfg_client_lock, NULL);
	if (rc != 0)
	{
		LOG_ERROR(("Init cfg_client_lock fail. RETURN %d", rc));
		goto quit;
	}
	rc = pthread_cond_init(&cc->ready_cond, NULL);
	if (rc != 0)
	{
		LOG_ERROR(("Init ready_cond fail. RETURN %d", rc));
		goto quit;
	}
	// 执行状态设置为OK
	cc->workstate = WORKING;
	FREE_CONFIG_ARRAY;
	LOG_INFO(("==============Config client init OK============"));
	return cc;

quit:
	LOG_ERROR(("Here come quit"));
	if(zh_bootstrap)
		zookeeper_close(zh_bootstrap);
	if (cc)
		cfg_destroy(cc);
	FREE_CONFIG_ARRAY;
	return NULL;
}

int cfg_get_data(cfgclient_t *cc, const char *key, char *value, int *value_len)
{
	if (!cc)
	{
		LOG_ERROR(("Config client handle is null"));
		return ERR_INVALID_HANDLE;
	}

	key_node_t *key_node = NULL;
	bool bexist = keynode_exist(cc, key, &key_node, true);

	if (!bexist)
	{
		if (cc->workstate != WORKING) 
		{ 
			LOG_ERROR(("config client is not avaliable now, try later...")); 
			return ERR_HANDLE_UNAVALIABLE; 
		}
		key_node = create_keynode(key, NULL, NULL);
		if (!key_node)
		{
			LOG_ERROR(("Create node for key [%s] return null", key));
			return ERR_CREATE_NODE;
		}
		// 加入队列
		int rc = enqueue_keynode(cc, key_node);
		if (0 != rc)
		{
			LOG_ERROR(("Exec enqueue_key_node error, return %d", rc));
			return ERR_ENQUEUE_NODE;
		}

		rc = keynode_attach(cc, key_node, true, 0);
		if (0 != rc)
		{
			LOG_ERROR(("Exec key_node_watch_and_up_data error, return %d", rc));
			return rc;
		}
	}
	return get_keynode_value(key_node, value, value_len);
}

int cfg_set_watcher(cfgclient_t *cc, const char *key, 
		cc_watcher_fn watcher, void *watcherCtx, 
		char *out_value, int *out_value_len)
{
	VALID_CHECK(cc);
	int rc = 0;
	key_node_t *key_node = NULL;
	bool bexist = keynode_exist(cc, key, &key_node, true);
	if (bexist)
	{
		if (!key_node)
		{
			LOG_ERROR(("key_node is null, abort"));
			return -1;
		}
		// 比较监听的客户端函数是否有变动，有则更新到队列节点中
		if (key_node->watch == 0
		 || key_node->watcher != watcher
		 || key_node->watcherctx != watcherCtx) 
		{
			// 更新节点的值(watcher & watcherCtx)
			rc = update_keynode_watcher(key_node, 1, watcher, watcherCtx);
			if (0 != rc)
			{
				LOG_ERROR(("Exec update_key_node error, return %d", rc));
				return rc;
			}
			// get新数据
			rc = keynode_attach(cc, key_node, true, 0);
			if (0 != rc)
			{
				LOG_ERROR(("key_node_call_zk_get error, return %d", rc));
				return rc;
			}
		}
		else
		{
			LOG_INFO(("nothing changed."));
		}
	}
	else // have not listened
	{	
		key_node = create_keynode(key, watcher, watcherCtx);
		if (!key_node)
		{
			LOG_ERROR(("Create node for key [%s] return null", key));
			return ERR_CREATE_NODE;
		}
		// 加入队列
		int rc = enqueue_keynode(cc, key_node);		
		if (0 != rc)
		{
			LOG_ERROR(("enqueue_key_node error, return %d", rc));
			destroy_keynode(key_node); // 加入队列失败需要free这个节点
			return ERR_ENQUEUE_NODE;
		}

		rc = keynode_attach(cc, key_node, true, 0);
		if (0 != rc)
		{
			LOG_ERROR(("key_node_call_zk_get error, return %d", rc));
			return rc;
		}
	}
	return get_keynode_value(key_node, out_value, out_value_len);
}

int cfg_clear_watcher(cfgclient_t *cc, const char *key)
{
	VALID_CHECK(cc);
	key_node_t *key_node = NULL;
	bool bexist = keynode_exist(cc, key, &key_node, true);
	// 更新节点的值(watcher & watcherCtx),停止watch
	// 当然啦，向zk的watch还是要继续的，这个key这辈子别想停下来了
	// 这里视情况而定吧，看要不要再继续
	if (bexist) {
		if (!key_node)
		{
			LOG_ERROR(("key_node is null, abort"));
			return -1;
		}
		int rc = update_keynode_watcher(key_node, 0, NULL, NULL);
		if (0 != rc)
		{
			LOG_ERROR(("Exec update_key_node error, return %d", rc));
			return rc;
		}
	}
	else 
	{
		LOG_INFO(("Key[%s] has not been watched", key));
	}

	return 0;
}

int cfg_destroy(cfgclient_t *cc)
{
	if (!cc) 
	{
		LOG_INFO(("Handle cc to destory is null, no need to destroy"));
	}
	pthread_mutex_lock(&cc->cfg_client_lock);
	cc->workstate = PENDING;
	zookeeper_close(cc->zh);
	pthread_rwlock_wrlock(&cc->queue_rwlock);
	keyqueue_destroy(cc); // free and assign null
	pthread_rwlock_unlock(&cc->queue_rwlock);
	pthread_mutex_unlock(&cc->cfg_client_lock);

	// 销毁锁
	pthread_rwlock_destroy(&cc->queue_rwlock);
	pthread_mutex_destroy(&cc->cfg_client_lock);
	pthread_cond_destroy(&cc->ready_cond);

	free(cc);
	cc = NULL;
	return 0;
}

void cfg_dump(cfgclient_t *cc, char *s)
{
	char dump[1024*8] = {0};
	sprintf(dump,
		"patition: 	%s \n"
		"workstate: 	%d \n"
		"zh_address: 	%p \n"
		"timeout:	%d \n"
		"path_prefix: 	%s \n"
		"zkhost: 	%s \n"
		"keynode_list: \n"		
		,cc->partition
		,cc->workstate
		,cc->zh
		,zoo_recv_timeout(cc->zh)
		,cc->path_prefix
		,cc->zkhost
		);
	key_node_t *qNode = cc->qhead;
	if(!qNode)
	{
		sprintf(dump, "%s\t[EMPTY]", dump);
	}
	else
	{
		for (;qNode!=NULL;qNode=qNode->next)
		{
			char skeynode[1024] = {0};
			sprintf(skeynode, 
				" +  KEYNODE [%s] address %p \n"
				"\tvalue=%s \n"
				"\tvalue_len=%d \n"
				"\twatch=%d \n"
				"\twatcher=%p \n"
				"\twatchCtx=%p \n"
				,qNode->key
				,qNode
				,qNode->value
				,qNode->value_len
				,qNode->watch
				,qNode->watcher
				,qNode->watcherctx
			);

			sprintf(dump, "%s%s",dump,skeynode);
		}
	}
	strcpy(s, dump);
}
