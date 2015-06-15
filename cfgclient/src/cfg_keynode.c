#include "cfg_keynode.h"
#include "cfg_util.h"
#include "zookeeper_log.h"
#include <unistd.h>
#include <string.h>

extern void zh_watcher(zhandle_t *zzh, int type, int state, const char *path, void* context);

int reload_zk(cfgclient_t *cc, int reconnect)
{
	LOG_INFO(("begin to reload client. %sneed reconnect zk", reconnect? "":"no "));
	if (!cc)
	{
		LOG_ERROR(("cc to reload is null"));
		return -1;
	}
	// 加大锁
	pthread_mutex_lock(&cc->cfg_client_lock);
	// state 变为 pending
	cc->workstate = PENDING;
	// 若需要重连zk的客户端
	if (reconnect)
	{
		// 关闭原来的zh
		zookeeper_close(cc->zh);
		cc->zh = NULL;
		// 重新初始化 zk 直到成功
		cc->zh = zookeeper_init(cc->zkhost, zh_watcher, 30000, 0, cc, 0);
		while (cc->zh == NULL)
		{
			sleep(3);
			cc->zh = zookeeper_init(cc->zkhost, zh_watcher, 30000, 0, cc, 0);
		}
	}
	// 重新注册队列
	pthread_rwlock_wrlock(&cc->queue_rwlock);
	key_node_t *pNode = cc->qhead;
	for (;pNode != NULL; pNode = pNode->next)
	{
		keynode_attach(cc, pNode, false, 1);
	}
	pthread_rwlock_unlock(&cc->queue_rwlock);

	// state 变为 working
	cc->workstate = WORKING;
	// 解大锁
	pthread_mutex_unlock(&cc->cfg_client_lock);
	// 通知等待workstate的各个线程
	pthread_cond_broadcast(&cc->ready_cond);
	return 0;
}

// 单独用来接收节点数据变化的通知
void keynode_watcher(zhandle_t *zzh, int type, int state, const char *path, void* context)
{
	LOG_DEBUG(("Watcher %s state = %s for path %s", type2String(type), state2String(state), 
		(path && strlen(path) > 0)? path : "[EMPTY]" ));

	// 补充先决条件的判断
	if (ZOO_CHANGED_EVENT != type && ZOO_NOTWATCHING_EVENT != type)
	{
		LOG_INFO(("No need event of this type. only need ZOO_CHANGED_EVENT & ZOO_NOTWATCHING_EVENT"));
		return;
	}
	if (!path)
	{
		LOG_ERROR(("path is null"));
		return;
	}

	if (!context)
	{
		LOG_ERROR(("Watcher callback context is null"));
		return;
	}
	// 处理业务
	cfgclient_t *cc = (cfgclient_t *)context;
	char key[32] = {0};
	int rc = parsekey(key, path, cc->path_prefix);
	if (0 != rc)
	{
		LOG_ERROR(("Parse key from full path [%s] error", path));
		return ;
	}

	key_node_t *key_node = NULL;
	// 在cc中寻找keynode,找不到则直接返回，找到则启动线程
	if (keynode_exist(cc, key, &key_node, true))
	{
		LOG_DEBUG(("Key [%s] found local, call new thread to process", key));
		LOG_DEBUG(("CC address 0x%x  Node address 0x%x", cc, key_node));
		// 启动线程来处理
		pthread_t tid;
		void **arg = malloc(2 * sizeof(cc)); // notice: 必须在处理线程中free掉
		arg[0] = cc;
		arg[1] = key_node;
		rc = pthread_create(&tid, NULL, process_watch_event, arg);
		if (0 != rc) 
		{
			LOG_ERROR(("Create thread for KEY [%s] change fail", key));
		}
	}
	else
	{
		LOG_ERROR(("404 Not found! for path [%s]", path));
	}
}
// 回调线程处理函数
void * process_watch_event(void *arg)
{
	void **args = (void **)arg;
	cfgclient_t *cc = (cfgclient_t *)args[0];
	key_node_t *key_node = (key_node_t *)args[1];
	free(arg);
	LOG_DEBUG(("CC address 0x%x  Node address 0x%x", cc, key_node));
	LOG_DEBUG(("A new thread is up to process Key [%s] change event", key_node->key));
	// 获取最新数据
	int rc = keynode_attach(cc, key_node, true, 1);
	if (0 != rc)
	{
		LOG_ERROR(("call zk to get node data and update error, return %d", rc));		
	}
	return ((void*)0);
	/*
	// 得到最新数据
	char value[1024 * 8] = {0};
    int value_len = sizeof(value);
	rc = get_keynode_value(key_node, value, &value_len);
	if (0 != rc)
	{
		LOG_ERROR(("get node value error, return %d", rc));
		return ((void*)0);
	}
	LOG_DEBUG(("Now begin to callback to application for Key [%s]", key_node->key));
	// 回调客户watcher
	if (key_node->watch && key_node->watcher != NULL)
	{
		(*key_node->watcher)(cc,key_node->key,value,value_len,key_node->watcherctx);
	}
	return ((void*)0);
	*/
}

/**************************************************
 * 下面为keynode 以及 queue 的内部具体实现
 **************************************************/

key_node_t * create_keynode(const char *key, cc_watcher_fn watcher, void *watcherCtx)
{
	LOG_DEBUG(("Begin to create keynode for KEY [%s]", key));
	if (strlen(key) > (KEY_MAX_LEN - 1))
	{
		LOG_ERROR(("Key length is larger than max_key_len[%d]. KEY [%s]", KEY_MAX_LEN, key));
		return NULL;
	}
	key_node_t *key_node = calloc(1, sizeof(key_node_t));
	if (!key_node)
	{
		LOG_ERROR(("Alloc memory error, return NULL. KEY [%s]", key));
		return NULL;
	}
	memset(key_node, 0, sizeof(key_node_t));
	strncpy(key_node->key, key, KEY_MAX_LEN);
	key_node->value = NULL;
	key_node->value_len = 0;
	if (watcher != NULL)
	{
		key_node->watch = 1;
	}
	key_node->watcher = watcher;
	key_node->watcherctx = watcherCtx;
	if (pthread_mutex_init(&key_node->keynode_lock, NULL)!=0)
	{
		LOG_ERROR(("Init mutex lock error"));
		free(key_node);
		return NULL;
	}
	return key_node;
}

// 更新节点的watcher信息
int update_keynode_watcher(key_node_t *key_node, int watch, cc_watcher_fn watcher, void *watcherCtx)
{
	if (key_node == NULL)
	{
		LOG_ERROR(("Invalid keynode data: keynode null"));
		return ERR_PARAM_INVALID;
	}
	pthread_mutex_lock(&key_node->keynode_lock);
	key_node->watch = watch;
	key_node->watcher = watcher;
	key_node->watcherctx = watcherCtx;
	pthread_mutex_unlock(&key_node->keynode_lock);
	return 0;
}

// 判断节点是否在队列中存在，若存在则返回true，且key_node为节点指针
// 默认参数needlock 加锁，无锁的情况适用于 enqueue_keynode中，在加写锁后，再判断一次条件，就无需再加读锁
bool keynode_exist(cfgclient_t *cc, const char *key, key_node_t **p_key_node, bool needlock)
{
	bool bexist = false;
	if (!cc)
	{
		LOG_ERROR(("cc is NULL, abort"));
		return bexist;
	}
	if (needlock)
	{
		pthread_rwlock_rdlock(&cc->queue_rwlock);
	}
	key_node_t *pNode = cc->qhead;
	for (; pNode != NULL; pNode = pNode->next)
	{
		if (strcmp(pNode->key, key) == 0)
		{
			bexist = true;
			*p_key_node = pNode;
			break;
		}
	}
	if (needlock)
	{
		pthread_rwlock_unlock(&cc->queue_rwlock);
	}
	LOG_DEBUG(("bexist=%s. key_node address 0x%x", bexist?"TRUE":"FALSE", *p_key_node));
	return bexist;
}

// 将keynode节点加入到队列中
int enqueue_keynode(cfgclient_t *cc, key_node_t *key_node)
{
	if (!cc || !key_node)
	{
		LOG_ERROR(("cc or key_node is NULL, abort"));
		return ERR_PARAM_INVALID;
	}
	pthread_rwlock_wrlock(&cc->queue_rwlock);
	key_node_t *key_node_tmp = NULL;
	bool bexist = keynode_exist(cc, key_node->key, &key_node_tmp, false);
	if (bexist)
	{
		LOG_ERROR(("Keynode [%s] has existed.", key_node->key));
		return ERR_PARAM_INVALID;
	}
	// 不存在则插入链表的头部
	key_node->prev = NULL;
	key_node->next = cc->qhead;
	if (NULL != cc->qhead)
	{
		cc->qhead->prev = key_node;
	}
	// 作为链表头
	cc->qhead = key_node;
	pthread_rwlock_unlock(&cc->queue_rwlock);
	return 0;
}

// 将keynode节点加入到队列中
int remove_keynode(cfgclient_t *cc, key_node_t *key_node)
{
	if (!cc || !key_node)
	{
		LOG_ERROR(("cc or key_node is NULL, abort"));
		return ERR_PARAM_INVALID;
	}
	pthread_rwlock_wrlock(&cc->queue_rwlock);
	key_node_t *key_node_prev = key_node->prev;
	key_node_t *key_node_next = key_node->next;
	if (cc->qhead == key_node)
	{
		cc->qhead = key_node->next;
	}
	else
	{
		key_node_prev->next = key_node_next;
		key_node_next->prev = key_node_prev;
	}
	destroy_keynode(key_node);
	pthread_rwlock_unlock(&cc->queue_rwlock);
	return 0;
}

// 注销双向链表
int keyqueue_destroy(cfgclient_t *cc)
{
	if (!cc)
	{
		LOG_ERROR(("cc is NULL, abort"));
		return 0;
	}
	pthread_rwlock_wrlock(&cc->queue_rwlock);
	key_node_t *pNode = cc->qhead;
	key_node_t *pNodeNext = NULL;
	while(NULL != pNode)
	{
		pNodeNext = pNode->next;
		destroy_keynode(pNode);
		pNode = pNodeNext;
	}
	cc->qhead = NULL;
	pthread_rwlock_unlock(&cc->queue_rwlock);
	return 0;
}

// 销毁keynode，且将指针置为null
int destroy_keynode(key_node_t *key_node)
{
	if (!key_node)
	{
		LOG_INFO(("No need to destroy keynode 'cause it is null now"));
		return 0;
	}
	if (key_node->value)
	{
		free(key_node->value);
	}
	pthread_mutex_destroy(&key_node->keynode_lock);
	free(key_node);
	key_node = NULL;
	return 0;
}

int get_keynode_value(key_node_t *key_node, char *value, int *value_len)
{
	if (!key_node)
	{
		LOG_ERROR(("key_node is NULL, abort"));
		return ERR_PARAM_INVALID;
	}
	pthread_mutex_lock(&key_node->keynode_lock);
	strncpy(value, key_node->value, key_node->value_len);
	*value_len = key_node->value_len;
	pthread_mutex_unlock(&key_node->keynode_lock);
	return 0;
}

// 更新keynode节点内存中的值
// return <0 error,  =0 no change, >0 changed
int update_keynode_value(key_node_t *key_node, const char *value, int value_len)
{	
	if (!key_node)
	{
		LOG_ERROR(("key_node is NULL, abort"));
		return ERR_PARAM_INVALID;
	}
	int changed = 1;
	pthread_mutex_lock(&key_node->keynode_lock);
	// 判断数据是否变化
	if ( key_node->value != 0
	  && value_len == key_node->value_len 
	  && strcmp(key_node->value, value) == 0)
	{
		LOG_DEBUG(("values hava no difference"));
		changed = 0;
	} 
	// 变化了则更新内存
	if (changed)
	{
		if (key_node->value)
		{
			free(key_node->value);
		}
		key_node->value = strdup(value);
		key_node->value_len = value_len;
	}
	pthread_mutex_unlock(&key_node->keynode_lock);
	return changed;
}

int keynode_attach(cfgclient_t *cc, key_node_t *key_node, bool need_check_state, int notify)
{
	if (!cc || !key_node)
	{
		LOG_ERROR(("cc or key_node is NULL, abort"));
		return ERR_PARAM_INVALID;
	}
	// 是否要判断工作状态 用条件锁 不然可能会丢消息
	if (need_check_state)
	{
		pthread_mutex_lock(&cc->cfg_client_lock);
		while (cc->workstate != WORKING)
		{
			pthread_cond_wait(&cc->ready_cond, &cc->cfg_client_lock);
			// 等待条件锁
		}
		pthread_mutex_unlock(&cc->cfg_client_lock);
	}
	
	// 调用zk wget数据并设置监听
	char buffer[1024 * 8] = {0};
    int buffer_len = sizeof(buffer);
    // 重试3次
    int rc = 0;
    int cnt = 0;
    char path[128] = {0};

    do {
    	rc = zoo_wget(cc->zh, fullpath(path, key_node->key, cc->path_prefix), 
    			keynode_watcher, cc, buffer, &buffer_len, NULL);
    	cnt++;
    } while (rc != 0 && cnt < 3);

	if (0 != rc)
	{
		LOG_ERROR(("zoo_wget error, return %d", rc));
		if (ZNONODE == rc)
		{
			remove_keynode(cc, key_node);
			return ERR_KEY_NOT_EXIST;
		}		
		return ERR_ZK_WGET_FAIL;
	}
	// 更新到缓存中
	rc = update_keynode_value(key_node, buffer, buffer_len);
	if (rc < 0)
	{
		LOG_ERROR(("update keynode value error, return %d", rc));
		return ERR_SYS_INTERNAL;
	}
	// 数据有变化情况下，若外部调用指明要通知(非用户调用都要指明notify为1)
	// 则判断watch和watcher有效 回调客户函数
	if (rc > 0 && notify && key_node->watch && key_node->watcher != NULL)
	{
		// 回调客户watcher
		LOG_INFO(("Now Callback to user watcher begin"));
		(*key_node->watcher)(cc,key_node->key,buffer,buffer_len,key_node->watcherctx);
		LOG_INFO(("Callback to user watcher end"));
	}
	return 0;
}
