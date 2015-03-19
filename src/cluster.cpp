/*
Copyright (c) 2012-2015 The SSDB Authors. All rights reserved.
Use of this source code is governed by a BSD-style license that can be
found in the LICENSE file.
*/
#include "cluster.h"
#include "ssdb/ssdb.h"
#include "util/log.h"
#include "cluster_store.h"
#include "cluster_migrate.h"

Cluster::Cluster(SSDB *db){
	log_debug("Cluster init");
	this->next_id = 1;
	this->db = db;
	this->store = new ClusterStore(db);
}

Cluster::~Cluster(){
	delete store;
	log_debug("Cluster finalized");
}

int Cluster::init(){
	int ret = this->store->load_kv_node_list(&kv_node_list);
	if(ret == -1){
		log_error("load_kv_node_list failed!");
		return -1;
	}
	std::vector<Node>::iterator it;
	for(it=kv_node_list.begin(); it!=kv_node_list.end(); it++){
		const Node &node = *it;
		if(node.id >= this->next_id){
			this->next_id = node.id + 1;
		}
	}
	return 0;
}

int Cluster::add_kv_node(const std::string &ip, int port){
	Locking l(&mutex);
	Node node;
	node.id = next_id ++;
	node.ip = ip;
	node.port = port;
	
	if(store->save_kv_node(node) == -1){
		return -1;
	}
	
	kv_node_list.push_back(node);
	return node.id;
}

int Cluster::del_kv_node(int id){
	Locking l(&mutex);
	std::vector<Node>::iterator it;
	for(it=kv_node_list.begin(); it!=kv_node_list.end(); it++){
		const Node &node = *it;
		if(node.id == id){
			if(store->del_kv_node(id) == -1){
				return -1;
			}
			kv_node_list.erase(it);
			return 1;
		}
	}
	return 0;
}

int Cluster::set_kv_range(int id, const KeyRange &range){
	Locking l(&mutex);
	std::vector<Node>::iterator it;
	for(it=kv_node_list.begin(); it!=kv_node_list.end(); it++){
		Node &node = *it;
		if(node.id == id){
			node.range = range;
			if(store->save_kv_node(node) == -1){
				return -1;
			}
			return 1;
		}
	}
	return 0;
}

int Cluster::get_kv_node_list(std::vector<Node> *list){
	Locking l(&mutex);
	*list = kv_node_list;
	return 0;
}

int Cluster::get_kv_node(int id, Node *ret){
	Locking l(&mutex);
	std::vector<Node>::iterator it;
	for(it=kv_node_list.begin(); it!=kv_node_list.end(); it++){
		Node &node = *it;
		if(node.id == id){
			*ret = node;
			return 1;
		}
	}
	return 0;
}

int64_t Cluster::migrate_kv_data(int src_id, int dst_id, int num_keys){
	Node src, dst;
	int ret;
	ret = get_kv_node(src_id, &src);
	if(ret != 1){
		return -1;
	}
	ret = get_kv_node(dst_id, &dst);
	if(ret != 1){
		return -1;
	}
	
	Locking l(&mutex);
	ClusterMigrate migrate;
	int64_t size = migrate.migrate_kv_data(&src, &dst, num_keys);
	if(size > 0){
		std::vector<Node>::iterator it;
		for(it=kv_node_list.begin(); it!=kv_node_list.end(); it++){
			Node &node = *it;
			if(src.id == node.id){
				src.range = node.range;
			}
			if(dst.id == node.id){
				dst.range = node.range;
			}
		}
	}
	return size;
}

