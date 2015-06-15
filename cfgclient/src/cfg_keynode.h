#ifndef __CFG_KEY_NODE_H__
#define __CFG_KEY_NODE_H__

#include "cfgclient.h"
#include "cfg_struct.h"
#include "stdbool.h"

// 重新连接zk,并将queue中的节点重新注册
int reload_zk(cfgclient_t *cc, int reconnect);

// 节点数据变化的watcher
void keynode_watcher(zhandle_t *zzh, int type, int state, const char *path, void* context);

// 处理节点变化通知
void * process_watch_event(void *arg);

// 创建关键字节点
key_node_t * create_keynode(const char *key, cc_watcher_fn watcher, void *watcherCtx);

// 更新节点的watcher信息
int update_keynode_watcher(key_node_t *key_node, int watch, cc_watcher_fn watcher, void *watcherCtx);

// 判断节点是否在队列中存在，若存在则返回true，且key_node为节点指针
// 默认参数needlock 加锁，无锁的情况适用于 enqueue_keynode中，在加写锁后，再判断一次条件，就无需再加读锁
bool keynode_exist(cfgclient_t *cc, const char *key, key_node_t **key_node, bool needlock);

// 将keynode节点加入到队列中
int enqueue_keynode(cfgclient_t *cc, key_node_t *key_node);

// 将节点删除
int remove_keynode(cfgclient_t *cc, key_node_t *key_node);

// 注销双向链表
int keyqueue_destroy(cfgclient_t *cc);

// 销毁keynode，且将指针置为null
int destroy_keynode(key_node_t *key_node);

// 获取keynode节点的值，这个值是从内存中取出的
int get_keynode_value(key_node_t *key_node, char *value, int *value_len);

// 更新keynode节点内存中的值
int update_keynode_value(key_node_t *key_node, const char *value, int value_len);

// keynode节点调用zk同步wget数据，且watch上，并将同步返回的数据更新到keynode中, notify代表是否要回调用户
// 用户主动触发的不需要去发通知，事件通知或者自发重连导致的重新拉取数据则需要通知用户
int keynode_attach(cfgclient_t *cc, key_node_t *key_node, bool need_check_state, int notify);

#endif
