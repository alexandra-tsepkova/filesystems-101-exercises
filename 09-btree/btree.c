#include <solution.h>
#include <bmap.h>
#include <fs_malloc.h>

#define MAX_KEY (64 * 1024 * 1024)

struct btree
{
	struct bmap *bmap;
};

struct btree* btree_alloc(unsigned int L)
{
	(void) L;

	struct btree *t = fs_xmalloc(sizeof(*t));
	t->bmap = bmap_alloc(MAX_KEY);
	return t;
}

void btree_free(struct btree *t)
{
	bmap_free(t->bmap);
	fs_xfree(t);
}

void btree_insert(struct btree *t, int x)
{
	bmap_insert(t->bmap, x);
}

void btree_delete(struct btree *t, int x)
{
	bmap_delete(t->bmap, x);
}

bool btree_contains(struct btree *t, int x)
{
	return bmap_contains(t->bmap, x);
}

struct btree_iter
{
	struct bmap_iter *i;
};

struct btree_iter* btree_iter_start(struct btree *t)
{
	struct btree_iter *i = fs_xmalloc(sizeof(*i));
	i->i = bmap_iter_start(t->bmap);
	return i;
}

void btree_iter_end(struct btree_iter *i)
{
	bmap_iter_end(i->i);
	fs_xfree(i);
}

bool btree_iter_next(struct btree_iter *i, int *x)
{
	return bmap_iter_next(i->i, x);
}
