#include <ntddk.h>
#include "../Monitor/ControlCode.h"
#include "FilterDriver.h"
#define implement_unordered_map
#include "unordered_map.h"

void unordered_map_Destructor(unordered_map *this);
void unordered_map_Insert(unordered_map *this,unsigned long long key,USERDATA UserData);
void unordered_map_Erase(unordered_map *this,void *address);
USERDATA* unordered_map_Get(unordered_map *this,unsigned long long key);

unordered_map * unordered_map_Constructor(int buckets)
{
	unordered_map *map=ExAllocatePool(NonPagedPool,sizeof(unordered_map));
	map->buckets=buckets;
	map->array=ExAllocatePool(NonPagedPool,map->buckets*2*sizeof(void*));
	memset(map->array,0,map->buckets*2*sizeof(void*));
	InitializeListHead(&map->head.list);
	map->size=0;
	ExInitializeNPagedLookasideList(&map->lookaside,NULL,NULL,0,sizeof(Node),0,0);
	map->Destructor=unordered_map_Destructor;
	map->Insert=unordered_map_Insert;
	map->Erase=unordered_map_Erase;
	map->Get=unordered_map_Get;
	return map;
}

void unordered_map_Destructor(unordered_map *this)
{
	ExDeleteNPagedLookasideList(&this->lookaside);
	ExFreePool(this->array);
	ExFreePool(this);
}

void unordered_map_Insert(unordered_map *this,unsigned long long key,USERDATA UserData)
{
	++this->size;
	Node *node=ExAllocateFromNPagedLookasideList(&this->lookaside);
	node->key=key;
	node->UserData=UserData;
	int index=key%this->buckets*2;
	if(this->array[index]==NULL)
	{
		if(this->head.list.Blink!=&this->head.list)
			this->array[((Node*)this->head.list.Blink)->key%this->buckets*2+1]=node;
		InsertTailList(&this->head.list,&node->list);
		this->array[index]=node;
		this->array[index+1]=(Node*)node->list.Flink;
	}
	else
	{
		//A<->C<->
		//B is node
		//B next is C
		node->list.Flink=this->array[index]->list.Flink;
		//A next is B
		this->array[index]->list.Flink=&node->list;
		//B preview is A
		node->list.Blink=&this->array[index]->list;
		//C preview is B
		node->list.Flink->Blink=&node->list;
	}
}

void unordered_map_Erase(unordered_map *this,void *address)
{
	if(address)
	{
		//A<->B<->C<->
		//B is node
		Node *node=CONTAINING_RECORD(address,Node,UserData);
		int index=node->key%this->buckets*2;
		if(this->array[index]==node)
		{
			if(node->list.Blink!=&this->head.list)
				this->array[((Node*)node->list.Blink)->key%this->buckets*2+1]=(Node*)node->list.Flink;
			if(this->array[index+1]==(Node*)node->list.Flink)
			{
				this->array[index]=NULL;
				this->array[index+1]=NULL;
			}
			else
				this->array[index]=(Node*)node->list.Flink;
		}
		//A next is C
		node->list.Blink->Flink=node->list.Flink;
		//C preview is A
		node->list.Flink->Blink=node->list.Blink;
		ExFreeToNPagedLookasideList(&this->lookaside,node);
		--this->size;
	}
}

USERDATA* unordered_map_Get(unordered_map *this,unsigned long long key)
{
	int index=key%this->buckets*2;
	for(Node *node=this->array[index];node&&node!=this->array[index+1];node=(Node*)node->list.Flink)
		if(node->key==key)
			return &node->UserData;
	return NULL;
}