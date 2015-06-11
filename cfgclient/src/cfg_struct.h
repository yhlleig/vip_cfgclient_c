#ifndef __CFG_STRUCT_H__
#define __CFG_STRUCT_H__

#include <pthread.h>
#include "zookeeper.h"

#define DEFAULT_CONFIG_CENTER \
	"GD6-cfgcenter-ZK-001.idc.vip.com:2181," \
	"GD6-cfgcenter-ZK-002.idc.vip.com:2181," \
	"GD6-cfgcenter-ZK-003.idc.vip.com:2181"

#define PENDING 0
#define WORKING 1

#define KEY_MAX_LEN 256

// bootstrap 解析出的配置结构体
typedef struct _bootstrap_config_t
{
	char 	partition_name[64];
	char	zk_host[512];
} bootstrap_config_t;

// 客户端维护的监听key的队列中，每个key为一个节点
// 队列为一个双向链表
typedef struct _key_node_t key_node_t;
struct _key_node_t
{
	char 					key[KEY_MAX_LEN];	// 监听的key
	char					*value;				// 值
	int 					value_len;			// 值的长度
	int						watch;				// 0 : don't watch; 1 : watch
	cc_watcher_fn 			watcher; 			// 用户自定义监听回调 
	void					*watcherctx;		// 用户程序空间内的指针，透传
	key_node_t 				*next;				// 双链
	key_node_t 				*prev;				// 向表
	pthread_mutex_t			keynode_lock;		// 保护节点的锁
};

// 配置中心客户端结构
struct _cfgclient
{
	char 					partition[32];		// 分区名称 默认thirdparty 
	int 					workstate;	 		// 是否正常工作
	zhandle_t 				*zh;				// 对应的zookeeper句柄
	char					path_prefix[32];	// 路径前缀，如/default/thirdparty
	char					zkhost[512];		// 业务zk集群host "1.1.1.1:2181,2.2.2.2:2181"
	key_node_t 				*qhead;				// 队列头--双向链表
	pthread_rwlock_t 		queue_rwlock;		// 保护队列的读写锁
	pthread_mutex_t 		cfg_client_lock; 	// 如若检测到zk集群异常，则会重置zh，过程[写锁队列-->mutexlock-->更新zh-->mutexunlock-->更新队列-->解锁队列]
	pthread_cond_t			ready_cond;			// 用于重置zk连接时候，有线程需要等待重置完成的条件变量
};

#endif
