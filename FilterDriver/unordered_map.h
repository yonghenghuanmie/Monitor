#pragma once

#ifdef implement_unordered_map
typedef struct Node
{
	LIST_ENTRY list;
	unsigned long long key;
	USERDATA UserData;
}Node;
#endif // implement_unordered_map

typedef struct unordered_map
{
	void (*Destructor)(struct unordered_map *this);
	void (*Insert)(struct unordered_map *this,unsigned long long key,USERDATA UserData);
	void (*Erase)(struct unordered_map *this,void *address);
	USERDATA* (*Get)(struct unordered_map *this,unsigned long long key);
#ifdef implement_unordered_map
	Node **array;
	int buckets;
	Node head;
	int size;
	NPAGED_LOOKASIDE_LIST lookaside;
#endif // implement_unordered_map
}unordered_map;

unordered_map* unordered_map_Constructor(int buckets);